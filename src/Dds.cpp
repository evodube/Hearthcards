// Hearthcards - collect trading cards and pin them to a poster in Skyrim Special Edition.
// Copyright (C) 2026 evodube
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program (LICENSE.txt).
// If not, see <https://www.gnu.org/licenses/>.

#include "Dds.h"

// Picture decoding and DXT1/DXT5 compression, based on Hearthframe's.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

namespace fs = std::filesystem;

namespace Dds
{
	namespace
	{
		struct Image
		{
			int                       w = 0, h = 0;
			std::vector<std::uint8_t> px;  // RGBA8
		};

		// Bilinear sample of a source rectangle into a w x h destination (good for up- and moderate down-scaling;
		// strong down-scaling is done first with a box filter, see Fit()).
		Image Resample(const Image& src, float sx0, float sy0, float sw, float sh, int w, int h)
		{
			Image out{ w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4) };
			for (int y = 0; y < h; ++y) {
				const float fy = std::clamp(sy0 + (y + 0.5f) * sh / h - 0.5f, 0.0f, static_cast<float>(src.h - 1));
				const int   y0 = static_cast<int>(fy);
				const int   y1 = (std::min)(y0 + 1, src.h - 1);
				const float ty = fy - y0;
				for (int x = 0; x < w; ++x) {
					const float fx = std::clamp(sx0 + (x + 0.5f) * sw / w - 0.5f, 0.0f, static_cast<float>(src.w - 1));
					const int   x0 = static_cast<int>(fx);
					const int   x1 = (std::min)(x0 + 1, src.w - 1);
					const float tx = fx - x0;
					const auto* a = &src.px[(static_cast<std::size_t>(y0) * src.w + x0) * 4];
					const auto* b = &src.px[(static_cast<std::size_t>(y0) * src.w + x1) * 4];
					const auto* c = &src.px[(static_cast<std::size_t>(y1) * src.w + x0) * 4];
					const auto* d = &src.px[(static_cast<std::size_t>(y1) * src.w + x1) * 4];
					auto*       o = &out.px[(static_cast<std::size_t>(y) * w + x) * 4];
					for (int k = 0; k < 4; ++k) {
						const float top = a[k] + (b[k] - a[k]) * tx;
						const float bot = c[k] + (d[k] - c[k]) * tx;
						o[k] = static_cast<std::uint8_t>(std::clamp(top + (bot - top) * ty + 0.5f, 0.0f, 255.0f));
					}
				}
			}
			return out;
		}

		// 2x2 box filter, used for big reductions and for the mip chain.
		Image Half(const Image& src)
		{
			const int w = (std::max)(1, src.w / 2);
			const int h = (std::max)(1, src.h / 2);
			Image     out{ w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4) };
			for (int y = 0; y < h; ++y) {
				const int y0 = (std::min)(y * 2, src.h - 1), y1 = (std::min)(y * 2 + 1, src.h - 1);
				for (int x = 0; x < w; ++x) {
					const int x0 = (std::min)(x * 2, src.w - 1), x1 = (std::min)(x * 2 + 1, src.w - 1);
					auto*     o = &out.px[(static_cast<std::size_t>(y) * w + x) * 4];
					for (int k = 0; k < 4; ++k) {
						const int s = src.px[(static_cast<std::size_t>(y0) * src.w + x0) * 4 + k] +
						              src.px[(static_cast<std::size_t>(y0) * src.w + x1) * 4 + k] +
						              src.px[(static_cast<std::size_t>(y1) * src.w + x0) * 4 + k] +
						              src.px[(static_cast<std::size_t>(y1) * src.w + x1) * 4 + k];
						o[k] = static_cast<std::uint8_t>((s + 2) / 4);
					}
				}
			}
			return out;
		}

		// Centre-crop to a_aspect (width / height), then scale to exactly w x h (stretching if the
		// aspects differ - the card mesh's UVs undo the stretch).
		// Pads the picture to a_aspect instead of cutting it: the picture stays whole and centred, the
		// added strips take the average colour of its outermost pixels (a card's own border colour), or are
		// fully transparent when the picture itself has transparency.
		Image PadToAspect(const Image& src, float a_aspect, bool a_transparent)
		{
			const float srcAspect = static_cast<float>(src.w) / src.h;
			int         pw = src.w, ph = src.h;
			if (std::fabs(srcAspect - a_aspect) < 0.002f) {
				return src;
			}
			if (srcAspect > a_aspect) {
				ph = static_cast<int>(std::lround(src.w / a_aspect));
			} else {
				pw = static_cast<int>(std::lround(src.h * a_aspect));
			}
			std::uint64_t sum[3]{};
			std::uint64_t n = 0;
			auto          add = [&](int x, int y) {
                const auto* p = &src.px[(static_cast<std::size_t>(y) * src.w + x) * 4];
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
                ++n;
			};
			for (int x = 0; x < src.w; ++x) {
				add(x, 0);
				add(x, src.h - 1);
			}
			for (int y = 0; y < src.h; ++y) {
				add(0, y);
				add(src.w - 1, y);
			}
			// a picture with transparency gets see-through strips; an opaque one its edge colour
			const std::uint8_t fill[4]{ static_cast<std::uint8_t>(sum[0] / n), static_cast<std::uint8_t>(sum[1] / n),
				static_cast<std::uint8_t>(sum[2] / n), static_cast<std::uint8_t>(a_transparent ? 0 : 255) };
			Image out;
			out.w = pw;
			out.h = ph;
			out.px.resize(static_cast<std::size_t>(pw) * ph * 4);
			for (std::size_t i = 0; i < out.px.size(); i += 4) {
				std::memcpy(&out.px[i], fill, 4);
			}
			const int ox = (pw - src.w) / 2, oy = (ph - src.h) / 2;
			for (int y = 0; y < src.h; ++y) {
				std::memcpy(&out.px[(static_cast<std::size_t>(y + oy) * pw + ox) * 4], &src.px[static_cast<std::size_t>(y) * src.w * 4],
					static_cast<std::size_t>(src.w) * 4);
			}
			return out;
		}

		Image Fit(Image src, int w, int h, float a_aspect)
		{
			const float targetAspect = a_aspect;
			float       cw = static_cast<float>(src.w), ch = static_cast<float>(src.h);
			if (cw / ch > targetAspect) {
				cw = ch * targetAspect;
			} else {
				ch = cw / targetAspect;
			}
			float cx = (src.w - cw) * 0.5f, cy = (src.h - ch) * 0.5f;
			// Box-halve while the crop is still more than twice the target size (avoids aliasing).
			while (cw >= 2.0f * w && ch >= 2.0f * h && src.w >= 2 && src.h >= 2) {
				src = Half(src);
				cw *= 0.5f;
				ch *= 0.5f;
				cx *= 0.5f;
				cy *= 0.5f;
			}
			return Resample(src, cx, cy, cw, ch, w, h);
		}

		// BC1 (DXT1, 8 bytes per 4x4 block) for opaque pictures, BC3 (DXT5, 16 bytes with smooth alpha) otherwise.
		void Compress(const Image& img, std::vector<std::uint8_t>& out, bool a_alpha)
		{
			const int bw = (std::max)(1, (img.w + 3) / 4);
			const int bh = (std::max)(1, (img.h + 3) / 4);
			std::uint8_t block[64];
			std::uint8_t dst[16];
			const int    size = a_alpha ? 16 : 8;
			for (int by = 0; by < bh; ++by) {
				for (int bx = 0; bx < bw; ++bx) {
					for (int py = 0; py < 4; ++py) {
						const int sy = (std::min)(by * 4 + py, img.h - 1);
						for (int px = 0; px < 4; ++px) {
							const int sx = (std::min)(bx * 4 + px, img.w - 1);
							std::memcpy(&block[(py * 4 + px) * 4], &img.px[(static_cast<std::size_t>(sy) * img.w + sx) * 4], 4);
						}
					}
					stb_compress_dxt_block(dst, block, a_alpha ? 1 : 0, STB_DXT_HIGHQUAL);
					out.insert(out.end(), dst, dst + size);
				}
			}
		}

		void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x)
		{
			for (int i = 0; i < 4; ++i) {
				v.push_back(static_cast<std::uint8_t>(x >> (i * 8)));
			}
		}

		// Classic DX9 DDS header (what Skyrim's loader expects for DXT1 / DXT5 / uncompressed BGRA).
		// a_fourCC: '1' = DXT1, '5' = DXT5, 0 = uncompressed BGRA.
		std::vector<std::uint8_t> Header(int w, int h, int mips, char a_fourCC, std::uint32_t linearSize)
		{
			std::vector<std::uint8_t> v;
			v.reserve(128);
			v.insert(v.end(), { 'D', 'D', 'S', ' ' });
			PutU32(v, 124);
			PutU32(v, 0x1 | 0x2 | 0x4 | 0x1000 | (mips > 1 ? 0x20000u : 0u) | (a_fourCC ? 0x80000u : 0x8u));
			PutU32(v, static_cast<std::uint32_t>(h));
			PutU32(v, static_cast<std::uint32_t>(w));
			PutU32(v, linearSize);
			PutU32(v, 0);  // depth
			PutU32(v, static_cast<std::uint32_t>(mips));
			for (int i = 0; i < 11; ++i) {
				PutU32(v, 0);
			}
			// pixel format
			PutU32(v, 32);
			if (a_fourCC) {
				PutU32(v, 0x4);  // DDPF_FOURCC
				v.insert(v.end(), { 'D', 'X', 'T', static_cast<std::uint8_t>(a_fourCC) });
				for (int i = 0; i < 5; ++i) {
					PutU32(v, 0);
				}
			} else {
				PutU32(v, 0x41);  // DDPF_RGB | DDPF_ALPHAPIXELS
				PutU32(v, 0);
				PutU32(v, 32);
				PutU32(v, 0x00FF0000);
				PutU32(v, 0x0000FF00);
				PutU32(v, 0x000000FF);
				PutU32(v, 0xFF000000);
			}
			PutU32(v, 0x1000 | (mips > 1 ? (0x8u | 0x400000u) : 0u));  // caps
			for (int i = 0; i < 4; ++i) {
				PutU32(v, 0);  // caps2..4, reserved2
			}
			return v;
		}

		bool WriteAtomically(const fs::path& a_out, const std::vector<std::uint8_t>& a_bytes, std::string& a_error)
		{
			std::error_code ec;
			fs::create_directories(a_out.parent_path(), ec);
			auto tmp = a_out;
			tmp += L".tmp";
			{
				FILE* f = nullptr;
				if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) {
					a_error = "cannot create " + tmp.string();
					return false;
				}
				const auto written = fwrite(a_bytes.data(), 1, a_bytes.size(), f);
				fclose(f);
				if (written != a_bytes.size()) {
					a_error = "write failed (disk full?)";
					fs::remove(tmp, ec);
					return false;
				}
			}
			if (!::MoveFileExW(tmp.c_str(), a_out.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				a_error = fmt::format("rename failed ({})", ::GetLastError());
				fs::remove(tmp, ec);
				return false;
			}
			return true;
		}
	}

	ReadResult ReadPictureFile(const fs::path& a_path)
	{
		ReadResult r;
		for (int attempt = 0; attempt < 5; ++attempt) {
			if (attempt > 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
			}
			HANDLE h = ::CreateFileW(a_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (h == INVALID_HANDLE_VALUE) {
				const DWORD e = ::GetLastError();
				r.notFound = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
				r.error = fmt::format("can't open the file (Windows error {}{})", e,
					r.notFound ? ", not found" : (e == ERROR_SHARING_VIOLATION ? ", in use by another program" : (e == ERROR_ACCESS_DENIED ? ", access denied" : "")));
				if (r.notFound) {
					return r;  // retrying won't help
				}
				continue;
			}
			LARGE_INTEGER size{};
			::GetFileSizeEx(h, &size);
			if (size.QuadPart <= 0 || size.QuadPart > (1ll << 30)) {
				::CloseHandle(h);
				r.error = size.QuadPart <= 0 ? "the file is empty (still being copied?)" : "the file is too big";
				continue;
			}
			r.bytes.resize(static_cast<std::size_t>(size.QuadPart));
			DWORD got = 0;
			const BOOL ok = ::ReadFile(h, r.bytes.data(), static_cast<DWORD>(r.bytes.size()), &got, nullptr);
			::CloseHandle(h);
			if (!ok || got != r.bytes.size()) {
				r.error = fmt::format("read failed (Windows error {})", ::GetLastError());
				continue;
			}
			r.error.clear();
			r.notFound = false;
			return r;
		}
		r.bytes.clear();
		return r;
	}

	unsigned char* DecodePicture(const fs::path& a_path, int& a_w, int& a_h, std::string& a_error, bool& a_notFound)
	{
		a_notFound = false;
		for (int attempt = 0; attempt < 3; ++attempt) {
			auto file = ReadPictureFile(a_path);
			if (!file.error.empty()) {
				a_error = file.error;
				a_notFound = file.notFound;
				return nullptr;
			}
			int  channels = 0;
			auto px = stbi_load_from_memory(file.bytes.data(), static_cast<int>(file.bytes.size()), &a_w, &a_h, &channels, 4);
			if (px) {
				a_error.clear();
				return px;
			}
			a_error = std::string("can't decode the picture: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown format");
			std::this_thread::sleep_for(std::chrono::milliseconds(300));  // maybe it was still being written
		}
		return nullptr;
	}

	void FreePixels(unsigned char* a_px)
	{
		stbi_image_free(a_px);
	}

	bool ConvertPicture(const fs::path& a_source, float a_aspect, int a_width, int a_height, const fs::path& a_out, std::string& a_error, FitMode a_mode)
	{
		Image src;
		bool  notFound = false;
		auto* px = DecodePicture(a_source, src.w, src.h, a_error, notFound);
		if (!px) {
			return false;
		}
		src.px.assign(px, px + static_cast<std::size_t>(src.w) * src.h * 4);
		stbi_image_free(px);
		// cards are opaque: any transparency in the picture is dropped (written as DXT1)
		constexpr bool transparent = false;
		for (std::size_t i = 3; i < src.px.size(); i += 4) {
			src.px[i] = 255;
		}

		float aspect = a_aspect;
		if (a_mode == FitMode::kPad) {
			src = PadToAspect(src, a_aspect, transparent);
		} else if (a_mode == FitMode::kStretch) {
			aspect = static_cast<float>(src.w) / static_cast<float>(src.h);  // keep all of it; the scale squeezes it onto the card
		}
		Image level = Fit(std::move(src), a_width, a_height, aspect);
		int   mips = 1;
		for (int w = a_width, h = a_height; w > 1 || h > 1; w = (std::max)(1, w / 2), h = (std::max)(1, h / 2)) {
			++mips;
		}

		std::vector<std::uint8_t> data;
		data.reserve(static_cast<std::size_t>(a_width) * a_height);
		for (int m = 0; m < mips; ++m) {
			Compress(level, data, transparent);
			if (m + 1 < mips) {
				level = Half(level);
			}
		}
		const auto linear = static_cast<std::uint32_t>((std::max)(1, (a_width + 3) / 4) * (std::max)(1, (a_height + 3) / 4) * (transparent ? 16 : 8));
		auto       bytes = Header(a_width, a_height, mips, transparent ? '5' : '1', linear);
		bytes.insert(bytes.end(), data.begin(), data.end());
		return WriteAtomically(a_out, bytes, a_error);
	}
}

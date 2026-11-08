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

#pragma once

// Picture -> DXT1 .dds conversion (from Hearthframe).
namespace Dds
{
	enum class FitMode
	{
		kStretch,  // the whole picture, squeezed/stretched to the card shape
		kPad,      // the whole picture, the leftover strip filled with its edge colour
		kCrop      // the middle of the picture, filling the card
	};

	struct ReadResult
	{
		std::vector<std::uint8_t> bytes;
		std::string               error;             // empty on success
		bool                      notFound = false;  // the file was listed but can't be opened by name (MO2/USVFS)
	};

	// Reads a whole file, sharing read/write/delete with other programs, retrying while it is locked or empty.
	ReadResult ReadPictureFile(const std::filesystem::path& a_path);

	// Decodes a picture file to RGBA8 with stb_image. Returns nullptr and fills a_error on failure; free with FreePixels().
	unsigned char* DecodePicture(const std::filesystem::path& a_path, int& a_w, int& a_h, std::string& a_error, bool& a_notFound);
	void           FreePixels(unsigned char* a_px);

	// Decodes a picture (png/jpg/bmp/tga), fits it to a_aspect (width / height) by a_mode (stretch, pad with
	// its own edge colour - or transparent strips for a picture with transparency - or centre-crop), scales it to
	// a_width x a_height, builds the full mip chain and writes a .dds: BC1 (DXT1) for opaque pictures, BC3 (DXT5)
	// when the picture has transparency (via a temp file, so the
	// game never sees half a texture). Returns false and fills a_error on failure.
	bool ConvertPicture(const std::filesystem::path& a_source, float a_aspect, int a_width, int a_height,
		const std::filesystem::path& a_out, std::string& a_error, FitMode a_mode = FitMode::kStretch);
}

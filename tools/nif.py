# Hearthcards - collect trading cards and pin them to a poster in Skyrim Special Edition.
# Copyright (C) 2026 evodube
#
# This program is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program (LICENSE.txt).
# If not, see <https://www.gnu.org/licenses/>.

"""A tiny writer for Skyrim Special Edition meshes (NIF 20.2.0.7, user version 12, BS version 100).

Only the handful of blocks Hearthcards needs: BSFadeNode, BSXFlags, BSInvMarker, BSTriShape,
BSLightingShaderProperty, BSShaderTextureSet, NiAlphaProperty, bhkBoxShape, bhkRigidBody,
bhkCollisionObject. Layouts follow niftools' nif.xml and were checked against vanilla meshes.
"""
import math
import struct

HAVOK_SCALE = 0.0142875  # game units -> havok units (1 / 69.99)

# Skyrim havok materials / layers used here
MAT_BOOK = 1264672850
MAT_WOOD = 500811281
LAYER_STATIC = 1
LAYER_CLUTTER = 4

SF1_DEFAULT = 0x82400301  # specular, receive + cast shadows, own emit, remappable textures, zbuffer test
SF2_DEFAULT = 0x8021      # zbuffer write, vertex colours, envmap light fade


def _f(*v):
    return struct.pack(f"<{len(v)}f", *v)


def _half(v):
    return struct.pack("<e", v)


def _nb(v):
    """normbyte: -1..1 -> 0..255"""
    return max(0, min(255, int(round((v + 1.0) * 127.5))))


class Mesh:
    """One BSTriShape: vertices (pos, uv, normal, tangent, bitangent) and triangles."""

    def __init__(self, name, texture, normal_map, flags=0xE):
        self.name = name
        self.texture = texture
        self.normal_map = normal_map
        self.flags = flags  # 0xF = hidden (APP_CULLED)
        self.verts = []
        self.tris = []
        # shader
        self.sf1 = SF1_DEFAULT
        self.sf2 = SF2_DEFAULT
        self.emissive = (0.0, 0.0, 0.0)
        self.emissive_mult = 1.0
        self.alpha = 1.0
        self.glossiness = 10.0
        self.spec_strength = 0.15
        self.alpha_flags = None  # NiAlphaProperty flags, or None for opaque

    def quad(self, p0, p1, p2, p3, uv=((0, 1), (1, 1), (1, 0), (0, 0))):
        """p0..p3 counter-clockwise seen from the front (bottom-left, bottom-right, top-right, top-left)."""
        ax, ay, az = (p1[i] - p0[i] for i in range(3))
        bx, by, bz = (p3[i] - p0[i] for i in range(3))
        n = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
        ln = math.sqrt(sum(c * c for c in n)) or 1.0
        n = tuple(c / ln for c in n)
        la = math.sqrt(ax * ax + ay * ay + az * az) or 1.0
        t = (ax / la, ay / la, az / la)  # along +U
        b = (n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0])
        base = len(self.verts)
        for p, (u, v) in zip((p0, p1, p2, p3), uv):
            self.verts.append((p, (u, v), n, t, b))
        self.tris.append((base, base + 1, base + 2))
        self.tris.append((base, base + 2, base + 3))

    def box(self, x0, x1, y0, y1, z0, z1, uv_front=None):
        """A box; the +Y face gets uv_front (or the full texture)."""
        f = uv_front or ((0, 1), (1, 1), (1, 0), (0, 0))
        e = ((0, 0), (0, 0), (0, 0), (0, 0))
        self.quad((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), f)  # +Y front... (seen from +Y)
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), e)  # -Y back
        self.quad((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), e)  # -X
        self.quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), e)  # +X
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), e)  # +Z top
        self.quad((x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0), e)  # -Z bottom

    def bound(self):
        if not self.verts:
            return (0, 0, 0), 0
        xs = [v[0][0] for v in self.verts]
        ys = [v[0][1] for v in self.verts]
        zs = [v[0][2] for v in self.verts]
        c = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2)
        r = max(math.dist(c, v[0]) for v in self.verts)
        return c, r


# vertex format: position, UV, normal, tangent, vertex colours (= vanilla static clutter)
VFLAGS = 0x3B
STRIDE = 32
VDESC = (STRIDE // 4) | (4 << 8) | (5 << 16) | (6 << 20) | (7 << 24) | (VFLAGS << 44)


def _vertex(v):
    (p, (u, vv), n, t, b) = v
    out = _f(*p) + _f(b[0])                      # position + bitangent X
    out += _half(u) + _half(vv)                  # UV
    out += bytes((_nb(n[0]), _nb(n[1]), _nb(n[2]), _nb(b[1])))  # normal + bitangent Y
    out += bytes((_nb(t[0]), _nb(t[1]), _nb(t[2]), _nb(b[2])))  # tangent + bitangent Z
    out += b"\xff\xff\xff\xff"                    # vertex colour (white)
    assert len(out) == STRIDE
    return out


class Nif:
    def __init__(self, root_name):
        self.blocks = []   # (type, builder)
        self.strings = []
        self.root_name = root_name

    def s(self, text):
        if text is None:
            return -1
        if text not in self.strings:
            self.strings.append(text)
        return self.strings.index(text)

    def add(self, btype, data=b""):
        self.blocks.append([btype, data])
        return len(self.blocks) - 1

    def set(self, idx, data):
        self.blocks[idx][1] = data

    # ---- block payloads
    def node(self, name, children, extra=(), collision=-1, flags=0xE):
        d = struct.pack("<iI", self.s(name), len(extra)) + struct.pack(f"<{len(extra)}i", *extra)
        d += struct.pack("<iI", -1, flags) + _f(0, 0, 0) + _f(1, 0, 0, 0, 1, 0, 0, 0, 1) + _f(1.0)
        d += struct.pack("<i", collision)
        d += struct.pack("<I", len(children)) + struct.pack(f"<{len(children)}i", *children) + struct.pack("<I", 0)
        return d

    def trishape(self, m, shader, alpha):
        c, r = m.bound()
        d = struct.pack("<iIiI", self.s(m.name), 0, -1, m.flags) + _f(0, 0, 0) + _f(1, 0, 0, 0, 1, 0, 0, 0, 1) + _f(1.0)
        d += struct.pack("<i", -1)
        d += _f(*c, r) + struct.pack("<iii", -1, shader, alpha) + struct.pack("<Q", VDESC)
        nv, nt = len(m.verts), len(m.tris)
        assert nv < 65536 and nt < 65536
        d += struct.pack("<HHI", nt, nv, STRIDE * nv + 6 * nt)
        d += b"".join(_vertex(v) for v in m.verts)
        d += b"".join(struct.pack("<3H", *t) for t in m.tris)
        d += struct.pack("<I", 0)  # particle data size
        return d

    def shader(self, m, texset):
        d = struct.pack("<IiIiII", 0, -1, 0, -1, m.sf1, m.sf2)
        d += _f(0, 0) + _f(1, 1) + struct.pack("<i", texset)
        d += _f(*m.emissive) + _f(m.emissive_mult) + struct.pack("<I", 3)
        d += _f(m.alpha, 0.0, m.glossiness) + _f(1, 1, 1) + _f(m.spec_strength) + _f(0.3, 2.0)
        assert len(d) == 100
        return d

    @staticmethod
    def texset(diffuse, normal):
        tex = [diffuse, normal, "", "", "", "", "", "", ""]
        d = struct.pack("<I", len(tex))
        for t in tex:
            b = t.encode("cp1252")
            d += struct.pack("<I", len(b)) + b
        return d

    @staticmethod
    def alpha_prop(flags, threshold=128):
        return struct.pack("<iIiHB", -1, 0, -1, flags, threshold)

    def bsx(self, value):
        return struct.pack("<iI", self.s("BSX"), value)

    def invmarker(self, rx, ry, rz, zoom):
        return struct.pack("<iHHHf", self.s("INV"), rx, ry, rz, zoom)

    @staticmethod
    def box_shape(half, material, radius=0.005):
        return struct.pack("<If", material, radius) + b"\0" * 8 + _f(*(h * HAVOK_SCALE for h in half)) + _f(0.0)

    @staticmethod
    def rigid_body(shape, layer, mass, half, dynamic):
        hx, hy, hz = (h * HAVOK_SCALE * 2 for h in half)  # full extents in havok units
        d = struct.pack("<iI", shape, layer)
        d += b"\0" * 4 + bytes((1, 0, 0, 0)) + struct.pack("<III", 0, 0, 0x80000000)  # world object cinfo
        d += bytes((1, 0)) + struct.pack("<H", 0xFFFF)                                     # entity cinfo
        d += b"\0" * 4 + struct.pack("<I", layer) + b"\0" * 4 + struct.pack("<I", 0)        # cinfo: filter
        d += bytes((1, 0)) + struct.pack("<H", 0xFFFF)                                     # response, delay
        d += _f(0, 0, 0, 0) + _f(0, 0, 0, 1) + _f(0, 0, 0, 0) + _f(0, 0, 0, 0)            # translation, rotation, velocities
        if dynamic:
            ix = mass / 12 * (hy * hy + hz * hz)
            iy = mass / 12 * (hx * hx + hz * hz)
            iz = mass / 12 * (hx * hx + hy * hy)
        else:
            ix = iy = iz = 0.0
        d += _f(ix, 0, 0, 0, 0, iy, 0, 0, 0, 0, iz, 0)                                     # inertia
        d += _f(0, 0, 0, 0)                                                                 # center
        d += _f(mass if dynamic else 0.0, 0.1, 0.05, 1.0, 1.0, 0.5, 0.0, 0.4, 104.4, 31.57, 0.02)
        if dynamic:   # motion SPHERE_STABILIZED, deactivator NEVER, solver LOW, quality MOVING
            d += bytes((3, 1, 2, 4, 0, 0, 3, 0))
        else:         # motion FIXED, deactivator NEVER, solver OFF, quality FIXED
            d += bytes((7, 1, 1, 1, 0, 0, 3, 0))
        d += b"\0" * 12 + struct.pack("<IH", 0, 0)                                         # unused, constraints, body flags
        assert len(d) == 250, len(d)
        return d

    @staticmethod
    def collision_object(target, body):
        return struct.pack("<iHi", target, 0x81, body)

    # ---- assemble
    def write(self, path):
        types = []
        for t, _ in self.blocks:
            if t not in types:
                types.append(t)
        h = b"Gamebryo File Format, Version 20.2.0.7\n"
        h += struct.pack("<IBIII", 0x14020007, 1, 12, len(self.blocks), 100)
        for s in ("evodube", "", "Hearthcards tools/nif.py"):
            b = s.encode() + b"\0"
            h += bytes((len(b),)) + b
        h += struct.pack("<H", len(types))
        for t in types:
            h += struct.pack("<I", len(t)) + t.encode()
        h += b"".join(struct.pack("<H", types.index(t)) for t, _ in self.blocks)
        h += b"".join(struct.pack("<I", len(d)) for _, d in self.blocks)
        h += struct.pack("<II", len(self.strings), max([len(s) for s in self.strings] or [0]))
        for s in self.strings:
            h += struct.pack("<I", len(s)) + s.encode("cp1252")
        h += struct.pack("<I", 0)
        body = b"".join(d for _, d in self.blocks)
        foot = struct.pack("<Ii", 1, 0)
        with open(path, "wb") as f:
            f.write(h + body + foot)


def build(path, root_name, meshes, collision=None, bsx=0x82, inv=None):
    """meshes: list of Mesh. collision: dict(half=(x,y,z), layer, mass, material, dynamic) or None.
    inv: (rx, ry, rz, zoom) for the inventory marker or None."""
    n = Nif(root_name)
    root = n.add("BSFadeNode")
    extra = []
    if inv:
        extra.append(n.add("BSInvMarker", n.invmarker(*inv)))
    extra.append(n.add("BSXFlags", n.bsx(bsx)))
    coll = -1
    if collision:
        shape = n.add("bhkBoxShape", n.box_shape(collision["half"], collision["material"]))
        body = n.add("bhkRigidBody", n.rigid_body(shape, collision["layer"], collision.get("mass", 0.0),
                                                  collision["half"], collision.get("dynamic", False)))
        coll = n.add("bhkCollisionObject", n.collision_object(root, body))
    children = []
    texsets = {}
    for m in meshes:
        idx = n.add("BSTriShape")
        sh = n.add("BSLightingShaderProperty")
        key = (m.texture, m.normal_map)
        if key not in texsets:
            texsets[key] = n.add("BSShaderTextureSet", n.texset(*key))
        al = n.add("NiAlphaProperty", n.alpha_prop(m.alpha_flags)) if m.alpha_flags is not None else -1
        n.set(idx, n.trishape(m, sh, al))
        n.set(sh, n.shader(m, texsets[key]))
        children.append(idx)
    n.set(root, n.node(root_name, children, extra, coll))
    n.write(path)

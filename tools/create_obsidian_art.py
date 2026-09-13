#!/usr/bin/env python3
"""Create the original Obsidian Arena textures and three cycle LODs.

No source images, fonts, models, network services, or random state are used.
Requires Pillow; all artwork is defined by the geometry in this file.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
CYAN = (57, 218, 243)
AMBER = (255, 161, 60)


def save(image, directory, name):
    image.save(directory / name, optimize=False, compress_level=9)


def textures(directory):
    # A 48-world-unit architectural floor tile. Thin cyan details are sparse;
    # the collision trails carry the strongest colors in the playing field.
    floor = Image.new("RGB", (512, 512), (13, 20, 26))
    d = ImageDraw.Draw(floor)
    d.rectangle((3, 3, 508, 508), outline=(24, 36, 44), width=2)
    d.line((0, 256, 512, 256), fill=(17, 27, 34), width=2)
    d.line((256, 0, 256, 512), fill=(17, 27, 34), width=2)
    for x, y in [(11, 11), (491, 11), (11, 491), (491, 491)]:
        d.line((x, y, x+10, y), fill=(36, 91, 109), width=2)
        d.line((x, y, x, y+10), fill=(36, 91, 109), width=2)
    save(floor, directory, "gltron_floor.png")

    for side in range(4):
        wall = Image.new("RGB", (1024, 256), (8, 14, 20))
        d = ImageDraw.Draw(wall)
        accent = CYAN if side % 2 == 0 else AMBER
        d.rectangle((0, 228, 1024, 245), fill=tuple(c//6 for c in accent))
        d.rectangle((0, 233, 1024, 236), fill=accent)
        d.rectangle((0, 12, 1024, 16), fill=tuple(c//3 for c in accent))
        for x in range(0, 1024, 128):
            d.polygon([(x+6, 220), (x+6, 43), (x+28, 24),
                       (x+111, 24), (x+121, 37), (x+121, 220)],
                      fill=(17, 25, 32), outline=(28, 40, 48))
            d.rectangle((x+28, 40, x+87, 42), fill=(42, 62, 72))
            d.rectangle((x+113, 102, x+116, 166), fill=tuple(c//2 for c in accent))
            for tick in range(3):
                d.rectangle((x+19+tick*10, 187, x+23+tick*10, 196), fill=(37, 59, 70))
        save(wall, directory, f"gltron_wall_{side+1}.png")

    # Seamless cube background: lateral faces share the same horizon gradient.
    for face in range(6):
        sky = Image.new("RGB", (512, 512))
        d = ImageDraw.Draw(sky)
        for y in range(512):
            horizon = math.exp(-((y-256)/96)**2)
            c = (round(4+4*horizon), round(8+13*horizon), round(14+20*horizon))
            if face in (1, 4):
                c = (4, 8, 14) if face == 1 else (5, 10, 17)
            d.line((0, y, 511, y), fill=c)
        save(sky, directory, f"skybox{face}.png")

    # Multiplicative trail mask, with bright, clean edges and a translucent core.
    trail = Image.new("RGBA", (256, 256))
    p = trail.load()
    for y in range(256):
        edge = math.exp(-min(y, 255-y)/5)
        level = round(174 + 81*edge)
        for x in range(256):
            p[x, y] = (level, level, level, round(176+79*edge))
    save(trail, directory, "gltron_traildecal.png")
    save(trail, directory, "gltron_trail.png")

    impact = Image.new("RGBA", (512, 512))
    p = impact.load()
    for y in range(512):
        for x in range(512):
            u, v = (x-255.5)/255.5, (y-255.5)/255.5
            radius = math.hypot(u, v)
            core = math.exp(-radius*radius*38)
            ring = math.exp(-((radius-.60)/.025)**2) * .48
            rays = math.exp(-abs(u)*65)*math.exp(-abs(v)*3)*.45
            alpha = max(0, min(1, core+ring+rays)) * max(0, 1-radius**5)
            p[x, y] = (197, 244, 255, round(alpha*255))
    save(impact, directory, "gltron_impact.png")

    # Menu plate and a geometric wordmark, independent of installed fonts.
    background = Image.new("RGB", (1024, 1024), (8, 14, 21))
    d = ImageDraw.Draw(background)
    for offset in range(-1024, 1025, 96):
        d.line((offset, 1024, offset+800, 0), fill=(13, 24, 34), width=2)
    d.polygon([(0, 750), (1024, 402), (1024, 423), (0, 771)], fill=(11, 48, 62))
    d.line((0, 750, 1024, 402), fill=(31, 126, 156), width=2)
    save(background, directory, "gltron.png")

    # A small original chamfered stroke alphabet for the wordmark.
    glyphs = {
        "A": [[(0,6),(0,1),(1,0),(3,0),(4,1),(4,6)],[(0,3),(4,3)]],
        "B": [[(0,6),(0,0),(3,0),(4,1),(4,2),(3,3),(0,3)],[(3,3),(4,4),(4,5),(3,6),(0,6)]],
        "D": [[(0,6),(0,0),(3,0),(4,1),(4,5),(3,6),(0,6)]],
        "E": [[(4,0),(0,0),(0,6),(4,6)],[(0,3),(3,3)]],
        "I": [[(0,0),(4,0)],[(2,0),(2,6)],[(0,6),(4,6)]],
        "K": [[(0,0),(0,6)],[(4,0),(1,3),(4,6)]],
        "L": [[(0,0),(0,6),(4,6)]],
        "N": [[(0,6),(0,0),(4,6),(4,0)]],
        "O": [[(1,0),(3,0),(4,1),(4,5),(3,6),(1,6),(0,5),(0,1),(1,0)]],
        "R": [[(0,6),(0,0),(3,0),(4,1),(4,2),(3,3),(0,3)],[(2,3),(4,6)]],
        "S": [[(4,0),(1,0),(0,1),(0,2),(1,3),(3,3),(4,4),(4,5),(3,6),(0,6)]],
        "T": [[(0,0),(4,0)],[(2,0),(2,6)]],
        "U": [[(0,0),(0,5),(1,6),(3,6),(4,5),(4,0)]],
        "V": [[(0,0),(2,6),(4,0)]],
        " ": [],
    }
    logo = Image.new("RGBA", (1024, 256))
    d = ImageDraw.Draw(logo)

    def label(text, top, cell, color):
        left = (1024-(len(text)*6-2)*cell)//2
        for letter in text:
            for path in glyphs[letter]:
                d.line([(left+x*cell,top+y*cell) for x,y in path],
                       fill=color,width=max(2,round(cell*.45)),joint="curve")
            left += cell*6

    label("VULKANTRON", 34, 16, (208, 245, 250, 255))
    d.rectangle((70, 166, 952, 168), fill=(*CYAN, 200))
    label("OBSIDIAN ARENA", 193, 5, (*CYAN, 255))
    save(logo, directory, "gltron_logo.png")


class Mesh:
    def __init__(self):
        self.vertices = []
        self.normals = []
        self.faces = []
        self.vertex_lookup = {}
        self.normal_lookup = {}

    def triangle(self, a, b, c, material):
        u = [b[i]-a[i] for i in range(3)]
        v = [c[i]-a[i] for i in range(3)]
        n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        length = math.sqrt(sum(x*x for x in n))
        assert length > 1e-8, "Degenerate triangle"
        n = tuple(round(x/length, 6) for x in n)
        if n not in self.normal_lookup:
            self.normal_lookup[n] = len(self.normals)+1
            self.normals.append(n)
        indices = []
        for point in (a, b, c):
            point = tuple(round(x, 6) for x in point)
            if point not in self.vertex_lookup:
                self.vertex_lookup[point] = len(self.vertices)+1
                self.vertices.append(point)
            indices.append(self.vertex_lookup[point])
        self.faces.append((material, indices, self.normal_lookup[n]))

    def quad(self, a, b, c, d, material):
        self.triangle(a, b, c, material)
        self.triangle(a, c, d, material)

    def ellipsoid(self, center, scale, material, segments=16, rings=7):
        def point(lat, lon):
            return tuple(center[i]+scale[i]*v for i, v in enumerate(
                (math.cos(lat)*math.cos(lon), math.cos(lat)*math.sin(lon), math.sin(lat))))
        for j in range(rings):
            lat0, lat1 = -math.pi/2+j*math.pi/rings, -math.pi/2+(j+1)*math.pi/rings
            for i in range(segments):
                lon0, lon1 = i*2*math.pi/segments, (i+1)*2*math.pi/segments
                a, b, c, d = point(lat0,lon0), point(lat0,lon1), point(lat1,lon1), point(lat1,lon0)
                if j != 0:
                    self.triangle(a,b,c,material)
                if j != rings-1:
                    self.triangle(a,c,d,material)

    def wheel(self, y, radius, half_width, segments):
        z = -1.52+radius  # ground level after the game's half-height placement
        for side in (-1, 1):
            x = side*half_width
            for i in range(segments):
                a, b = i*2*math.pi/segments, (i+1)*2*math.pi/segments
                def p(angle, r, xx=x):
                    return (xx, y+r*math.cos(angle), z+r*math.sin(angle))
                # Disk, two illuminated rings, and dark tread all have geometry.
                for inner, outer, material in [(0.0,.55*radius,"Armor"),
                                                (.55*radius,.60*radius,"Hull"),
                                                (.60*radius,.86*radius,"Armor"),
                                                (.86*radius,.94*radius,"Hull"),
                                                (.94*radius,radius,"Armor")]:
                    if inner == 0:
                        tri = [(x,y,z),p(a,outer),p(b,outer)]
                        if side < 0:
                            tri.reverse()
                        self.triangle(*tri,material)
                    else:
                        quad = [p(a,inner),p(a,outer),p(b,outer),p(b,inner)]
                        if side < 0:
                            quad.reverse()
                        self.quad(*quad,material)
                if side == 1:
                    for left, right, material in [(-half_width,-half_width+.10,"Hull"),
                                                 (-half_width+.10,half_width-.10,"Rubber"),
                                                 (half_width-.10,half_width,"Hull")]:
                        self.quad(p(a,radius,left),p(b,radius,left),
                                  p(b,radius,right),p(a,radius,right),material)

    def export(self, path):
        assert len(self.vertices) < 30000 and len(self.normals) < 30000
        assert len(self.faces) < 20000
        lines = ["# Original VulkanTron Obsidian cycle; generated by create_obsidian_art.py",
                 "mtllib lightcycle.mtl"]
        lines += ["v " + " ".join(f"{v:.6f}" for v in p) for p in self.vertices]
        lines += ["vn " + " ".join(f"{v:.6f}" for v in p) for p in self.normals]
        for material in sorted(set(f[0] for f in self.faces)):
            lines.append("usemtl "+material)
            for mat, indices, normal in self.faces:
                if mat == material:
                    lines.append("f "+" ".join(f"{i}//{normal}" for i in indices))
        path.write_text("\n".join(lines)+"\n")
        return {"vertices": len(self.vertices), "normals": len(self.normals),
                "triangles": len(self.faces), "bounds": [[min(p[i] for p in self.vertices),
                max(p[i] for p in self.vertices)] for i in range(3)]}


def models(directory):
    # Z bounds symmetric about zero; model remains 10 units long, matching
    # classic camera/trail placement. Forward is -Y; no collision code changes.
    stats = {}
    for name, segments, rings in [("high", 32, 10), ("med", 20, 7), ("low", 12, 5)]:
        mesh = Mesh()
        mesh.wheel(-3.45, 1.42, .69, segments)
        mesh.wheel(3.25, 1.48, .76, segments)
        mesh.ellipsoid((0, .20, -.15), (1.0, 3.30, .69), "Armor", segments, rings)
        # A continuous, low forward fairing and raised rider canopy.
        mesh.ellipsoid((0, -.70, .46), (.71, 2.05, .67), "Armor", segments, rings)
        mesh.ellipsoid((0, -1.35, .85), (.50, 1.16, .67), "Glass", segments, rings)
        for x in (-.925, .925):
            mesh.ellipsoid((x, .20, -.07), (.075, 2.34, .085), "Hull", segments//2, 4)
        mesh.ellipsoid((0, -1.14, 1.505), (.045, .84, .015), "Hull", segments//2, 4)
        stats[name] = mesh.export(directory/f"lightcycle-{name}.obj")
    material = """# Original procedural Obsidian Arena materials.
newmtl Armor
Ka 0.035 0.055 0.073
Kd 0.095 0.135 0.170
Ks 0.48 0.62 0.72
Ns 48
newmtl Glass
Ka 0.012 0.024 0.032
Kd 0.030 0.052 0.070
Ks 0.70 0.84 0.92
Ns 80
newmtl Hull
Ka 0.1 0.6 0.7
Kd 0.1 0.8 1.0
Ks 0.6 0.9 1.0
Ns 16
newmtl Rubber
Ka 0.012 0.017 0.024
Kd 0.022 0.030 0.040
Ks 0.10 0.13 0.16
Ns 8
"""
    (directory/"lightcycle.mtl").write_text(material)
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT/"art"/"obsidian")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    textures(args.output)
    stats = models(args.output)
    manifest = {"generator": "tools/create_obsidian_art.py", "source": "Original procedural geometry",
                "models": stats, "sha256": {}}
    for path in sorted(args.output.iterdir()):
        if path.suffix in (".png", ".obj", ".mtl"):
            manifest["sha256"][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (args.output/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()

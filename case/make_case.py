# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""
Enclosure for the linky-matter-over-thread board, built with FreeCAD.

Run it with FreeCAD's command line, from the repository root:

    freecadcmd case/make_case.py

It writes the FreeCAD model, a STEP file and one STL per printed part to
case/. If case/build/board.step exists (exported from KiCad, see the
README), it also checks that the board and its parts do not collide with
the enclosure, and renders a preview.

Coordinates: X and Y follow the PCB with the origin at its bottom-left
corner as seen from the top (KiCad y is flipped: Y = 41 - y_kicad), Z
starts under the floor of the base.
"""

import math
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = HERE
BUILD = os.path.join(HERE, "build")
V = App.Vector

# ---------------------------------------------------------------- board (from the KiCad layout)
PCB_W, PCB_H, PCB_T = 64.0, 41.0, 1.6


def k(x, y):
    """KiCad board coordinates (mm, y down) to enclosure X, Y."""
    return x, PCB_H - y


H1 = k(3.8, 3.8)               # M3 holes
H2 = k(60.2, 37.2)
SW1 = k(51.25, 4.25)           # pairing button, Diptronics DTSA-62N, actuator 5 mm above the board
LED = k(57.76, 5.6)            # status LED D4
J1_SCREWS = [k(6.0, y) for y in (12.84, 17.92, 23.0)]   # A, I1, I2
J1_LABELS = ["A", "I1", "I2"]
C1 = k(17.0, 5.8)              # 470 uF, 10 x 16 mm

# ---------------------------------------------------------------- enclosure parameters
GAP = 0.5          # board to wall
WALL = 2.0
FLOOR = 2.0
ROOF = 2.0
STANDOFF = 5.0     # room for the through-hole leads under the board (trimmer: 4.4 mm)
TALLEST = 17.0     # C1 is 16 mm plus its seal, above the board
HEADROOM = 1.4
TONGUE = 0.85      # base tongue, lid skirt overlap
FIT = 0.15
LIP_H = 2.0
R_OUT = 3.0

Z_PCB_BOT = FLOOR + STANDOFF
Z_PCB_TOP = Z_PCB_BOT + PCB_T
Z_ROOF_IN = Z_PCB_TOP + TALLEST + HEADROOM
Z_TOP = Z_ROOF_IN + ROOF

X0, X1 = -GAP, PCB_W + GAP          # inside of the walls
Y0, Y1 = -GAP, PCB_H + GAP


def rbox(x0, y0, z0, x1, y1, z1, r):
    """Box with its vertical edges rounded."""
    b = Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))
    if r <= 0:
        return b
    edges = [e for e in b.Edges
             if abs(e.Vertexes[0].Point.x - e.Vertexes[1].Point.x) < 1e-6
             and abs(e.Vertexes[0].Point.y - e.Vertexes[1].Point.y) < 1e-6]
    return b.makeFillet(r, edges)


def ring(off_in, off_out, z0, z1):
    """Band following the walls, between two offsets from the inner wall face."""
    outer = rbox(X0 - off_out, Y0 - off_out, z0, X1 + off_out, Y1 + off_out, z1, 1.0 + off_out)
    inner = rbox(X0 - off_in, Y0 - off_in, z0, X1 + off_in, Y1 + off_in, z1, 1.0 + off_in)
    return outer.cut(inner)


def cyl(r, x, y, z0, z1):
    return Part.makeCylinder(r, z1 - z0, V(x, y, z0))


def text(s, size, x, y, z, depth):
    """Engraving tool: extruded text, left-aligned at x, y."""
    font = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
    wires = Part.makeWireString(s, font, size, 0)
    faces = []
    for char in wires:
        if not char:
            continue
        # Outer contour first, holes (A, O...) cut afterwards.
        char = sorted(char, key=lambda w: -Part.Face(w).Area)
        f = Part.Face(char[0])
        for hole in char[1:]:
            f = f.cut(Part.Face(hole))
        faces.append(f)
    shape = Part.makeCompound(faces)
    shape.translate(V(x, y, z - depth))
    return shape.extrude(V(0, 0, depth + 1.0))


WIRE_SLOT = dict(y0=k(0, 25.6)[1], y1=k(0, 10.2)[1], z0=Z_PCB_TOP, z1=Z_PCB_TOP + 9.0)


def wire_slot():
    s = WIRE_SLOT
    return Part.makeBox(WALL + 2.0, s["y1"] - s["y0"], s["z1"] - s["z0"],
                        V(X0 - WALL - 1.0, s["y0"], s["z0"]))


# ---------------------------------------------------------------- base
def make_base():
    base = rbox(X0 - WALL, Y0 - WALL, 0, X1 + WALL, Y1 + WALL, Z_PCB_TOP, R_OUT)
    base = base.cut(rbox(X0, Y0, FLOOR, X1, Y1, Z_TOP, 1.0))
    base = base.fuse(ring(0, TONGUE, Z_PCB_TOP, Z_PCB_TOP + LIP_H))

    for (x, y) in (H1, H2):
        base = base.fuse(cyl(3.5, x, y, 0, Z_PCB_BOT))
    # Plain supports under the two other corners.
    for (x, y) in ((3.5, 3.5), (60.5, 37.5)):
        base = base.fuse(cyl(2.5, x, y, FLOOR - 0.1, Z_PCB_BOT))

    # Mounting ears on the long sides.
    for y_edge, sign in ((Y0 - WALL, -1), (Y1 + WALL, 1)):
        ear = rbox(PCB_W / 2 - 6, min(y_edge - sign, y_edge + sign * 7), 0,
                   PCB_W / 2 + 6, max(y_edge - sign, y_edge + sign * 7), 3.0, 2.0)
        ear = ear.cut(cyl(2.1, PCB_W / 2, y_edge + sign * 3.8, -1, 4))
        base = base.fuse(ear)

    # M3 screws from below: clearance hole and socket head counterbore.
    for (x, y) in (H1, H2):
        base = base.cut(cyl(1.7, x, y, -1, Z_PCB_BOT + 1))
        base = base.cut(cyl(3.1, x, y, -1, 3.2))

    base = base.cut(wire_slot())
    return base.removeSplitter()


# ---------------------------------------------------------------- lid
BUTTON_TUBE_Z = Z_PCB_TOP + 6.6      # bottom of the plunger guide
LED_TUBE_Z = Z_PCB_TOP + 1.9         # just above the LED (1.1 mm tall)


def make_lid():
    lid = rbox(X0 - WALL, Y0 - WALL, Z_PCB_TOP, X1 + WALL, Y1 + WALL, Z_TOP, R_OUT)
    lid = lid.cut(rbox(X0, Y0, 0, X1, Y1, Z_ROOF_IN, 1.0))
    # Skirt: only the outer part of the wall goes down over the base tongue.
    lid = lid.cut(ring(-0.01, TONGUE + FIT, Z_PCB_TOP - 0.01, Z_PCB_TOP + LIP_H))

    # Pillars that clamp the board and take the M3 screws (2.6 mm pilot for thread-forming screws).
    for (x, y) in (H1, H2):
        lid = lid.fuse(cyl(3.5, x, y, Z_PCB_TOP, Z_ROOF_IN + 0.1))
        lid = lid.cut(cyl(1.3, x, y, Z_PCB_TOP - 1, Z_PCB_TOP + 12))

    # Screwdriver access to the terminal block, and its labels.
    for (x, y), label in zip(J1_SCREWS, J1_LABELS):
        lid = lid.cut(cyl(2.25, x, y, Z_ROOF_IN - 1, Z_TOP + 1))
        lid = lid.cut(text(label, 3.5, x + 3.6, y - 1.75, Z_TOP, 0.6))

    # Pairing button: hole and guide tube for the printed plunger.
    bx, by = SW1
    lid = lid.fuse(cyl(2.75, bx, by, BUTTON_TUBE_Z, Z_ROOF_IN + 0.1))
    lid = lid.cut(cyl(1.8, bx, by, BUTTON_TUBE_Z - 1, Z_TOP + 1))

    # Status LED: a dark tube down to the LED and a window in the roof.
    lx, ly = LED
    lid = lid.fuse(cyl(2.25, lx, ly, LED_TUBE_Z, Z_ROOF_IN + 0.1))
    lid = lid.cut(cyl(1.5, lx, ly, LED_TUBE_Z - 1, Z_TOP + 1))

    lid = lid.cut(text("LINKY TIC", 5.0, 22.0, 6.0, Z_TOP, 0.6))
    lid = lid.cut(wire_slot())
    return lid.removeSplitter()


# ---------------------------------------------------------------- plunger
SW_TOP = Z_PCB_TOP + 5.0


def make_plunger():
    bx, by = SW1
    z0 = SW_TOP + 0.3
    flange = cyl(3.0, bx, by, z0, z0 + 1.0)
    shaft = cyl(1.5, bx, by, z0, Z_TOP + 1.4)
    return flange.fuse(shaft).removeSplitter()


# ---------------------------------------------------------------- board envelope, for the fit check
def board_envelope(step_path):
    import Import

    doc = App.newDocument("board")
    Import.insert(step_path, doc.Name)
    shapes = [o.Shape for o in doc.Objects if hasattr(o, "Shape") and o.Shape.Solids]
    # Keep only top-level placed shapes (inside the board footprint), not library copies.
    # The placed assembly is the shape inside the board area with the most solids.
    placed = [s for s in shapes if s.BoundBox.XMin >= 95 and s.BoundBox.YMax <= -95]
    board = max(placed, key=lambda s: len(s.Solids)).copy()
    # KiCad STEP: board from (100, -141) to (164, -100), bottom face at z = 0.
    board.translate(V(-100.0, 141.0, Z_PCB_BOT))
    App.closeDocument(doc.Name)
    # Parts whose KiCad model is missing or shorter than the real part.
    extra = [
        cyl(5.25, C1[0], C1[1], Z_PCB_TOP, Z_PCB_TOP + TALLEST),                     # C1 at full height
        Part.makeBox(18.0, 13.0, 2.2, V(45.0, PCB_H - 25.5, Z_PCB_TOP)),             # E73 module
        Part.makeBox(6.0, 6.0, 5.0, V(SW1[0] - 3.0, SW1[1] - 3.0, Z_PCB_TOP)),      # SW1 at its real 5 mm height
    ]
    return Part.makeCompound([board] + extra)


def interference(a, b):
    try:
        return a.common(b).Volume
    except Exception:
        return float("nan")


# ---------------------------------------------------------------- preview
def render(parts, path, azim=-55, elev=30, size=(1400, 1050), dim_below=None):
    """Small depth-buffer renderer (orthographic, flat shading), so previews need no GUI.

    dim_below darkens faces lying under a given height, to make engravings readable from above.
    """
    import numpy as np
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    a, e = math.radians(azim), math.radians(elev)
    # Camera looking at the origin from (azim, elev); rows are the screen x, screen y and depth axes.
    right = np.array([math.cos(a), math.sin(a), 0.0])
    fwd = np.array([-math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), -math.sin(e)])
    up = np.cross(right, fwd)
    rot = np.vstack([right, up, -fwd])
    light = np.array([0.35, -0.45, 0.82])
    light /= np.linalg.norm(light)

    tris, cols = [], []
    for shape, color, offset, tol in parts:
        pts, idx = shape.tessellate(tol)
        if not idx:
            continue
        p = np.array([[v.x, v.y, v.z] for v in pts]) + np.array(offset)
        t = p[np.array(idx)]
        n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        lambert = np.abs(n @ light)
        shade = 0.35 + 0.65 * lambert
        if dim_below is not None:
            shade = np.where(t[:, :, 2].max(1) < dim_below, shade * 0.55, shade)
        c = np.array(matplotlib.colors.to_rgb(color))
        tris.append(t)
        cols.append(np.clip(c[None, :] * shade[:, None], 0, 1))
    tris = np.concatenate(tris) @ rot.T
    cols = np.concatenate(cols)

    w, h = size
    lo, hi = tris.reshape(-1, 3).min(0), tris.reshape(-1, 3).max(0)
    scale = 0.92 * min(w / (hi[0] - lo[0]), h / (hi[1] - lo[1]))
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    sx = (tris[..., 0] - cx) * scale + w / 2
    sy = h / 2 - (tris[..., 1] - cy) * scale
    sz = tris[..., 2]

    img = np.ones((h, w, 3))
    zbuf = np.full((h, w), -np.inf)
    for i in range(len(tris)):
        x0, x1 = int(max(0, np.floor(sx[i].min()))), int(min(w - 1, np.ceil(sx[i].max())))
        y0, y1 = int(max(0, np.floor(sy[i].min()))), int(min(h - 1, np.ceil(sy[i].max())))
        if x1 < x0 or y1 < y0:
            continue
        (ax_, ay), (bx, by), (cx_, cy_) = zip(sx[i], sy[i])
        den = (by - cy_) * (ax_ - cx_) + (cx_ - bx) * (ay - cy_)
        if abs(den) < 1e-9:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((by - cy_) * (xs - cx_) + (cx_ - bx) * (ys - cy_)) / den
        l2 = ((cy_ - ay) * (xs - cx_) + (ax_ - cx_) * (ys - cy_)) / den
        l3 = 1 - l1 - l2
        inside = (l1 >= -2e-3) & (l2 >= -2e-3) & (l3 >= -2e-3)
        if not inside.any():
            continue
        z = l1 * sz[i][0] + l2 * sz[i][1] + l3 * sz[i][2]
        zb = zbuf[y0:y1 + 1, x0:x1 + 1]
        hit = inside & (z > zb)
        zb[hit] = z[hit]
        img[y0:y1 + 1, x0:x1 + 1][hit] = cols[i]
    plt.imsave(path, img)


# ---------------------------------------------------------------- main
def main():
    base, lid, plunger = make_base(), make_lid(), make_plunger()

    doc = App.newDocument("linky_case")
    for name, shape in (("Base", base), ("Lid", lid), ("Plunger", plunger)):
        doc.addObject("Part::Feature", name).Shape = shape
    doc.recompute()
    doc.saveAs(os.path.join(OUT, "linky_case.FCStd"))

    import Import
    import MeshPart

    Import.export(doc.Objects, os.path.join(OUT, "linky_case.step"))
    # STL in print orientation: base on its floor, lid on its roof, plunger on its flange.
    for name, shape, flip in (("base", base, False), ("lid", lid, True), ("plunger", plunger, False)):
        s = shape.copy()
        if flip:
            s.rotate(V(0, 0, 0), V(1, 0, 0), 180)
        s.translate(V(0, 0, -s.BoundBox.ZMin))
        mesh = MeshPart.meshFromShape(Shape=s, LinearDeflection=0.02, AngularDeflection=0.2, Relative=False)
        mesh.write(os.path.join(OUT, f"linky_case_{name}.stl"))

    bb = base.fuse(lid).BoundBox
    print(f"outside: {bb.XLength:.1f} x {bb.YLength:.1f} x {bb.ZLength:.1f} mm (with the ears)")
    print(f"volumes: base {base.Volume / 1000:.1f} cm3, lid {lid.Volume / 1000:.1f} cm3, "
          f"plunger {plunger.Volume / 1000:.2f} cm3")
    print(f"base and lid overlap: {interference(base, lid):.3f} mm3")

    step = os.path.join(BUILD, "board.step")
    if os.path.exists(step):
        board = board_envelope(step)
        for name, shape in (("base", base), ("lid", lid), ("plunger", plunger)):
            print(f"board vs {name}: {interference(board, shape):.3f} mm3")
        os.makedirs(os.path.join(HERE, "images"), exist_ok=True)
        lift = Z_TOP - Z_PCB_TOP + 12
        render([(base, "#5b6472", (0, 0, 0), 0.05), (board, "#2f7a45", (0, 0, 0), 0.08),
                (plunger, "#f59e0b", (0, 0, lift), 0.03), (lid, "#cfd5dd", (0, 0, lift), 0.05)],
               os.path.join(HERE, "images", "exploded.png"), azim=-38, elev=30)
        render([(base, "#5b6472", (0, 0, 0), 0.05), (lid, "#cfd5dd", (0, 0, 0), 0.05),
                (plunger, "#f59e0b", (0, 0, 0), 0.03)],
               os.path.join(HERE, "images", "closed.png"), azim=-38, elev=30)
        render([(lid, "#cfd5dd", (0, 0, 0), 0.05), (plunger, "#f59e0b", (0, 0, 0), 0.03)],
               os.path.join(HERE, "images", "top.png"), azim=0, elev=90, size=(1200, 850),
               dim_below=Z_TOP - 0.1)
        print("previews written to case/images/")


main()

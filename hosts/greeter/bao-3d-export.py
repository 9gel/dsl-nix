"""Export the 3D bao chip for bao-3d.c.

Source: dimsumlabs-graphics 3D/model_for_dslan.blend. Keeps the
bao (Cube.002), the chip body (Cube.001) and the pins
(Cube.006). The cable, plug and LEDs are left out, and the
RJ45 socket is cut out of the bao.
Modifiers are applied. The pins are split into two walking
groups: A and B step in turn, like a trotting insect.

  Blender -b model_for_dslan.blend \
    --python bao-3d-export.py -- OUT_DIR

Writes bao-3d-body.glb, bao-3d-legs-a.glb, bao-3d-legs-b.glb.
glTF is Y-up: Blender X stays X, Blender Z is Y.
"""

import sys
from pathlib import Path

import bmesh
import bpy

OUT = Path(sys.argv[sys.argv.index("--") + 1])
BAO = (0.97, 0.95, 0.92, 1)
CHIP = (0.90, 0.90, 0.91, 1)
PIN = (0.80, 0.80, 0.83, 1)


def material(name, rgba):
    mat = bpy.data.materials.new(name)
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = rgba
    bsdf.inputs["Roughness"].default_value = 0.6
    return mat


def baked(name, rgba):
    """New object: the source mesh with modifiers applied."""
    src = bpy.data.objects[name]
    # The depsgraph here is the viewport one. Match the render.
    for mod in src.modifiers:
        mod.show_viewport = mod.show_render
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(src.evaluated_get(deps))
    mesh.transform(src.matrix_world)
    mesh.materials.clear()
    mesh.materials.append(material(name + "-mat", rgba))
    for poly in mesh.polygons:
        poly.material_index = 0
        poly.use_smooth = True
    obj = bpy.data.objects.new(name + "-baked", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def plug_socket(src):
    """Cut the RJ45 socket out of the bao and patch the hole.

    The socket sits on the bao's +Y side. The box is in the
    bao's local units, found from its axis-aligned faces.
    """
    bm = bmesh.new()
    bm.from_mesh(src.data)
    doomed = [
        v for v in bm.verts
        if abs(v.co.x) < 0.37 and v.co.y > 0.6 and 1.74 < v.co.z < 4.13
    ]
    bmesh.ops.delete(bm, geom=doomed, context="VERTS")
    rim = [e for e in bm.edges if e.is_boundary]
    patch = bmesh.ops.holes_fill(bm, edges=rim, sides=0)["faces"]
    bmesh.ops.triangulate(bm, faces=patch)
    bm.to_mesh(src.data)
    bm.free()


def pin_groups(obj):
    """Split pins into A and B meshes by loose part.

    Pins stand in two rows at +Y and -Y, four along X. A is
    rows' 1st and 3rd on +Y with 2nd and 4th on -Y.
    """
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    parts = []
    seen = set()
    for vert in bm.verts:
        if vert.index in seen:
            continue
        stack = [vert]
        part = []
        seen.add(vert.index)
        while stack:
            v = stack.pop()
            part.append(v.co.copy())
            for edge in v.link_edges:
                other = edge.other_vert(v)
                if other.index not in seen:
                    seen.add(other.index)
                    stack.append(other)
        parts.append(part)
    bm.free()
    if len(parts) != 8:
        raise SystemExit(f"expected 8 pins, got {len(parts)}")
    centres = [
        (sum(c.x for c in p) / len(p), sum(c.y for c in p) / len(p))
        for p in parts
    ]
    group_a = set()
    for side in (1, -1):
        row = sorted(
            (x, i) for i, (x, y) in enumerate(centres) if y * side > 0
        )
        start = 0 if side > 0 else 1
        group_a.update(i for _, i in row[start::2])
    return group_a, centres


def keep_pins(obj, keep):
    """Delete every loose pin whose index is not in keep."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    seen = {}
    part = 0
    for vert in bm.verts:
        if vert.index in seen:
            continue
        stack = [vert]
        seen[vert.index] = part
        while stack:
            v = stack.pop()
            for edge in v.link_edges:
                other = edge.other_vert(v)
                if other.index not in seen:
                    seen[other.index] = part
                    stack.append(other)
        part += 1
    doomed = [v for v in bm.verts if seen[v.index] not in keep]
    bmesh.ops.delete(bm, geom=doomed, context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()


def export(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objs:
        obj.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=str(OUT / name),
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_apply=True,
        export_normals=True,
        export_vertex_color="NONE",
        export_animations=False,
    )


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    plug_socket(bpy.data.objects["Cube.002"])
    bao = baked("Cube.002", BAO)
    chip = baked("Cube.001", CHIP)
    export([bao, chip], "bao-3d-body.glb")

    pins_a = baked("Cube.006", PIN)
    group_a, _ = pin_groups(pins_a)
    pins_b = baked("Cube.006", PIN)
    keep_pins(pins_a, group_a)
    keep_pins(pins_b, set(range(8)) - group_a)
    export([pins_a], "bao-3d-legs-a.glb")
    export([pins_b], "bao-3d-legs-b.glb")


main()

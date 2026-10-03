#!/usr/bin/env python3
"""Bake ChevroletSS's credited CC BY 4.0 Porsche 911 GT3 glTF for Metal.

Usage: python3 tools/import_porsche_gt3.py scene.gltf assets/vehicles/porsche_gt3
NumPy is an offline development dependency only. No runtime asset downloads.
"""
import json
import struct
import sys
from pathlib import Path
import numpy as np


def main(source, destination):
    gltf = json.loads(source.read_text())
    buffers = [(source.parent / b['uri']).read_bytes() for b in gltf['buffers']]

    def accessor(index):
        a = gltf['accessors'][index]
        b = gltf['bufferViews'][a['bufferView']]
        dtype = np.dtype({5121: 'u1', 5123: '<u2', 5125: '<u4', 5126: '<f4'}[a['componentType']])
        columns = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        return np.ndarray((a['count'], columns), dtype, buffer=buffers[b['buffer']],
            offset=b.get('byteOffset', 0) + a.get('byteOffset', 0),
            strides=(b.get('byteStride', columns * dtype.itemsize), dtype.itemsize)).copy()

    def transform(node):
        if 'matrix' in node:
            return np.array(node['matrix']).reshape(4, 4).T
        x, y, z, w = node.get('rotation', [0, 0, 0, 1])
        r = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                      [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                      [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        m = np.eye(4)
        m[:3, :3] = r @ np.diag(node.get('scale', [1, 1, 1]))
        m[:3, 3] = node.get('translation', [0, 0, 0])
        return m

    parts = []

    def visit(index, parent, name=''):
        node = gltf['nodes'][index]
        matrix = parent @ transform(node)
        name = node.get('name', name) if 'mesh' not in node else name
        if 'mesh' in node:
            # Source also includes blurred wheel duplicates and hidden damage
            # glass; these are alternative render states, not additional parts.
            if 'blur' in name.lower() or 'DamageGlass' in name:
                return
            for p in gltf['meshes'][node['mesh']]['primitives']:
                assert p.get('mode', 4) == 4
                a = p['attributes']
                positions = np.c_[accessor(a['POSITION']), np.ones(gltf['accessors'][a['POSITION']]['count'])] @ matrix.T
                normals = accessor(a['NORMAL']) @ np.linalg.inv(matrix[:3, :3])
                normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1e-10)
                indices = accessor(p['indices']).reshape(-1).astype('<u4')
                if np.linalg.det(matrix[:3, :3]) < 0:
                    indices = indices.reshape(-1, 3)[:, ::-1].reshape(-1).copy()
                parts.append(dict(name=name, p=positions[:, :3], n=normals,
                                  material=p.get('material', 0), indices=indices))
        for child in node.get('children', []):
            visit(child, matrix, name)

    for root in gltf['scenes'][gltf.get('scene', 0)]['nodes']:
        visit(root, np.eye(4))
    bounds = np.concatenate([p['p'] for p in parts])
    # Authored forward is +Z (verified against headlight/taillight positions).
    # Scale to a 4.55 m 911 body and put the tyre contact patch at Y=0.
    scale = 4.55 / np.ptp(bounds[:, 2])
    rotation = np.eye(3)
    offset = np.array([0., -bounds[:, 1].min(), 0.]) * scale
    for part in parts:
        part['p'] = (part['p'] @ rotation) * scale + offset
        part['n'] = part['n'] @ rotation
    tyre_parts = [p for p in parts if 'tyre' in p['name'].lower()]
    assert len(tyre_parts) == 4, [p['name'] for p in tyre_parts]
    centers = np.zeros((5, 4), dtype='<f4')
    for tyre in tyre_parts:
        lo, hi = tyre['p'].min(axis=0), tyre['p'].max(axis=0)
        center = (lo + hi) / 2
        wheel = (1 if center[0] < 0 else 2) if center[2] > 0 else (3 if center[0] < 0 else 4)
        centers[wheel] = [*center, (hi[1]-lo[1])/2]
    assert np.all(centers[1:, 3] > .25), centers
    # Move the longitudinal origin to the CG used by the chassis model: front
    # axle 1.45 m ahead, rear axle 1.0 m behind. Preserve authored proportions.
    shift_z = 1.45 - float(centers[1:3, 2].mean())
    centers[1:, 2] += shift_z
    for part in parts:
        part['p'][:, 2] += shift_z

    materials = []
    for i, m in enumerate(gltf['materials']):
        p = m.get('pbrMetallicRoughness', {})
        base = p.get('baseColorFactor', [1, 1, 1, 1]).copy()
        metal, rough = p.get('metallicFactor', 1), max(.06, p.get('roughnessFactor', 1))
        glass, coat, brake = 0, 0, 0
        emission = m.get('emissiveFactor', [0, 0, 0])
        if i == 43:  # Paint; retain the authored orange-red with a polished coat.
            metal, rough, coat = .45, .2, 1
        if i in (5, 40, 47, 49):
            metal, rough = .85, .25
        if i == 6:
            base, metal, rough = [.009, .009, .009, 1], 0, .85
        if i == 52:
            base, glass, rough = [.035, .055, .065, 1], 1, .08
        if i == 44:
            base, emission, brake = [.3, .003, .01, 1], [.12, .005, .005], 1
        if i == 41:  # Clear lamp covers, render behind the opaque parts.
            base[3] = .16
            rough = .08
        if i == 50:  # Untextured logo surfaces do not become white stickers.
            base, metal = [.1, .1, .1, 1], .8
        materials.append([*base, metal, rough, coat, glass, *emission, brake])

    vertex_dtype = [('p', '<f4', 3), ('n', '<f4', 3), ('uv', '<f4', 2), ('material', '<u4'), ('wheel', '<u4')]
    vertices, primitives, vertex_count = [], [], 0
    for part in parts:
        if part['material'] == 53:  # Fully transparent interior window faces.
            continue
        v = np.zeros(len(part['p']), dtype=vertex_dtype)
        v['p'], v['n'], v['material'] = part['p'], part['n'], part['material']
        name = part['name'].lower()
        rolling = any(token in name for token in ('tyre', 'rim', 'wheel_hub', 'a_disc'))
        caliper = 'caliper' in name
        if rolling or caliper:
            centroid = part['p'].mean(axis=0)
            wheel = int(np.argmin(np.linalg.norm(centers[1:, :3]-centroid, axis=1))) + 1
            # Reject any named part spanning more than one axle.
            assert np.ptp(part['p'][:, 2]) < 1.1, part['name']
            v['wheel'] = wheel | (8 if caliper else 0)  # calipers steer but never roll
        vertices.append(v)
        primitives.append((part['material'], part['indices'] + vertex_count))
        vertex_count += len(v)
    mesh = np.concatenate(vertices)
    indices = np.concatenate([indices for _, indices in sorted(primitives, key=lambda p: materials[p[0]][3] < .99)])
    destination.mkdir(parents=True, exist_ok=True)
    with (destination / 'car.mesh').open('wb') as f:
        f.write(struct.pack('<4s4I', b'ESC1', len(mesh), len(indices), len(materials), len(centers)))
        f.write(centers.tobytes())
        f.write(np.asarray(materials, dtype='<f4').tobytes())
        f.write(mesh.tobytes())
        f.write(indices.astype('<u4').tobytes())
    print('vertices', len(mesh), 'triangles', len(indices)//3, 'bounds', mesh['p'].min(axis=0), mesh['p'].max(axis=0))
    print('wheel centers/radii', centers.tolist())
    print('source bounds', bounds.min(axis=0), bounds.max(axis=0), 'metres/unit', scale)


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]))

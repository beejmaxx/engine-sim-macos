#!/usr/bin/env python3
"""Bake credited glTF cars using a checked-in, model-specific import profile.

Run offline with NumPy, Pillow, DracoPy and meshoptimizer. All expensive work
happens here; the game loads the resulting ESC2 buffers before starting audio.
"""
import argparse
import json
import re
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from import_road_cars import Gltf


def convert(source, profile, output):
    cfg = json.loads(profile.read_text())
    gltf = Gltf(source)
    parts = [p for p in gltf.parts()
             if p['material'] not in cfg.get('skip_materials', [])
             and not any(re.search(pattern, p['name']) for pattern in cfg.get('skip_parts', []))]
    forward = np.asarray(cfg['forward'], dtype=float)
    up = np.asarray([0, 1, 0], dtype=float)
    transform = np.stack([np.cross(forward, up), up, forward], axis=1)
    assert np.allclose(transform.T @ transform, np.eye(3))
    for p in parts:
        p['p'], p['n'] = p['p'] @ transform, p['n'] @ transform
        if np.linalg.det(transform) < 0:
            p['indices'] = p['indices'].reshape(-1, 3)[:, ::-1].reshape(-1).copy()
        if p['material'] in cfg.get('smooth_materials', []):
            # Weld only the lighting normals across exporter-split vertices;
            # keep the original silhouette, UV seams and deliberate hard edges.
            _, group = np.unique(np.round(p['p'], 5), axis=0, return_inverse=True)
            clusters = {}
            for index, key in enumerate(group):
                clusters.setdefault(int(key), []).append(index)
            source_normals = p['n'].copy()
            threshold = np.cos(np.deg2rad(cfg.get('smooth_angle', 50)))
            for indices in clusters.values():
                normals = source_normals[indices]
                for index in indices:
                    n = normals[normals @ source_normals[index] >= threshold].sum(axis=0)
                    p['n'][index] = n / max(np.linalg.norm(n), 1e-10)

    tyre_parts = [p for p in parts if p['material'] in cfg.get('tyre_materials', [])
                  or any(re.search(pattern, p['name']) for pattern in cfg.get('tyre_parts', []))]
    tyres = np.concatenate([p['p'] for p in tyre_parts])
    all_positions = np.concatenate([p['p'] for p in parts])
    scale = cfg['length'] / np.ptp(all_positions[:, 2])
    midpoint = (tyres.min(axis=0) + tyres.max(axis=0)) / 2
    # Identify four wheel positions even when an exporter merged all tyres/rims
    # into a single primitive. The profile restricts animation to wheel parts.
    wheels = np.zeros((5, 4), dtype='<f4')
    for wheel in range(1, 5):
        left, front = wheel in (1, 3), wheel in (1, 2)
        group = tyres[((tyres[:, 0] < midpoint[0]) == left)
                      & ((tyres[:, 2] > midpoint[2]) == front)]
        assert len(group), f'Missing wheel {wheel}'
        low, high = group.min(axis=0), group.max(axis=0)
        wheels[wheel] = [*((low + high) / 2 * scale), (high[1] - low[1]) / 2 * scale]
    offset = np.array([-midpoint[0] * scale, -tyres[:, 1].min() * scale,
                       1.45 - wheels[1:3, 2].mean()])
    wheels[1:, :3] += offset
    for p in parts:
        p['p'] = p['p'] * scale + offset
    assert np.all((wheels[1:, 3] > .15) & (wheels[1:, 3] < .7)), wheels

    output.mkdir(parents=True, exist_ok=True)
    textures, materials, material_map = {}, [], {}

    def make_material(index, part_name):
        override = {}
        for rule in cfg.get('part_materials', []):
            if re.search(rule['pattern'], part_name):
                override.update(rule['values'])
        key = (index, json.dumps(override, sort_keys=True))
        if key in material_map:
            return material_map[key]
        source_material = gltf.doc['materials'][index]
        pbr = source_material.get('pbrMetallicRoughness', {})
        values = dict(base=pbr.get('baseColorFactor', [1, 1, 1, 1]).copy(),
                      metal=pbr.get('metallicFactor', 1),
                      rough=max(.08, pbr.get('roughnessFactor', 1)), coat=0, glass=0,
                      emission=source_material.get('emissiveFactor', [0, 0, 0]).copy(), brake=0)
        values.update(cfg.get('materials', {}).get(str(index), {}))
        values.update(override)
        layer = -1
        texture = pbr.get('baseColorTexture', {}).get('index')
        if texture is not None and values.get('use_texture', True):
            if texture not in textures:
                textures[texture] = len(textures)
                gltf.image(texture).resize((512, 512), Image.Resampling.LANCZOS).save(
                    output / f'texture_{textures[texture]}.png')
            layer = textures[texture]
        result = len(materials)
        material_map[key] = result
        materials.append([*values['base'], values['metal'], values['rough'], values['coat'],
                          values['glass'], *values['emission'], values['brake'], layer, 0, 0, 0])
        return result

    dtype = np.dtype([('p', '<f4', 3), ('n', '<f4', 3), ('uv', '<f4', 2),
                      ('material', '<u4'), ('wheel', '<u4')])
    vertices, primitives, vertex_count = [], [], 0
    original_triangles = sum(len(p['indices']) // 3 for p in parts)
    reduction = min(1, cfg.get('triangle_budget', 180000) / original_triangles)
    for part in parts:
        material = make_material(part['material'], part['name'])
        indices = part['indices']
        if reduction < 1 and len(indices) > 3000:
            import meshoptimizer
            reduced = np.empty_like(indices)
            count = meshoptimizer.simplify(reduced, indices, np.asarray(part['p'], dtype='<f4'),
                                          target_index_count=max(300, int(len(indices) * reduction) // 3 * 3),
                                          target_error=cfg.get('simplify_error', .002))
            indices = reduced[:count]
        used, compact_indices = np.unique(indices, return_inverse=True)
        v = np.zeros(len(used), dtype=dtype)
        v['p'], v['n'], v['uv'] = part['p'][used], part['n'][used], part['uv'][used]
        v['material'] = material
        wheel_part = (part['material'] in cfg.get('wheel_materials', [])
                      or any(re.search(pattern, part['name']) for pattern in cfg.get('wheel_parts', [])))
        fixed_part = (part['material'] in cfg.get('fixed_wheel_materials', [])
                      or any(re.search(pattern, part['name']) for pattern in cfg.get('fixed_wheel_parts', [])))
        if wheel_part or fixed_part:
            # No wheel triangle may straddle two pivots; that would stretch the
            # body or axle during steering. Keep non-wheel chassis parts out.
            distances = np.linalg.norm(v['p'][:, None, :] - wheels[None, 1:, :3], axis=2)
            ids = np.argmin(distances, axis=1).astype('<u4') + 1
            triangle_ids = ids[compact_indices].reshape(-1, 3)
            assert np.all(triangle_ids == triangle_ids[:, :1]), part['name']
            v['wheel'] = ids | (8 if fixed_part else 0)
        vertices.append(v)
        primitives.append((material, compact_indices.astype('<u4') + vertex_count))
        vertex_count += len(v)

    vertices = np.concatenate(vertices)
    indices = np.concatenate([indices for material, indices in
                              sorted(primitives, key=lambda p: materials[p[0]][3] < .99)])
    assert len(materials) <= 64 and len(textures) <= 32
    assert np.isfinite(vertices['p']).all() and np.isfinite(vertices['n']).all()
    assert set(vertices['wheel'] & 7) == {0, 1, 2, 3, 4}
    assert np.max(np.abs(np.linalg.norm(vertices['n'], axis=1) - 1)) < .001
    with (output / 'car.mesh').open('wb') as f:
        f.write(struct.pack('<4s4I', b'ESC2', len(vertices), len(indices), len(materials), 5))
        f.write(wheels.tobytes())
        f.write(np.asarray(materials, dtype='<f4').tobytes())
        f.write(vertices.tobytes())
        f.write(indices.astype('<u4').tobytes())
    report = dict(vertices=len(vertices), triangles=len(indices) // 3,
                  source_triangles=original_triangles, materials=len(materials), texture_layers=len(textures),
                  bounds=[vertices['p'].min(axis=0).tolist(), vertices['p'].max(axis=0).tolist()],
                  wheels=wheels.tolist())
    (output / 'mesh-info.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    convert(args.source, args.profile, args.output)

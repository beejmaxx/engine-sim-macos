#!/usr/bin/env python3
"""Offline glTF/GLB conversion for the credited road cars (NumPy/Pillow/DracoPy).

Runtime uses only the baked ESC2 mesh and resized base-colour maps. No Python,
glTF parsing, decompression, downloads or image processing occurs while driving.
"""
import argparse
import io
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image


class Gltf:
    def __init__(self, source):
        self.source = source
        raw = source.read_bytes()
        if raw[:4] == b'glTF':
            length, kind = struct.unpack_from('<II', raw, 12)
            assert kind == 0x4e4f534a
            self.doc = json.loads(raw[20:20+length])
            n, kind = struct.unpack_from('<II', raw, 20+length)
            assert kind == 0x004e4942
            binary = raw[28+length:28+length+n]
        else:
            self.doc = json.loads(raw)
            binary = None
        self.buffers = [(source.parent / b['uri']).read_bytes() if 'uri' in b else binary
                        for b in self.doc['buffers']]

    def view(self, index):
        b = self.doc['bufferViews'][index]
        start = b.get('byteOffset', 0)
        return self.buffers[b['buffer']][start:start+b['byteLength']]

    def accessor(self, index):
        a = self.doc['accessors'][index]
        b = self.doc['bufferViews'][a['bufferView']]
        dtype = np.dtype({5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2',
                          5125: '<u4', 5126: '<f4'}[a['componentType']])
        columns = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        result = np.ndarray((a['count'], columns), dtype, buffer=self.buffers[b['buffer']],
                            offset=b.get('byteOffset', 0)+a.get('byteOffset', 0),
                            strides=(b.get('byteStride', columns*dtype.itemsize), dtype.itemsize)).copy()
        if a.get('normalized'):
            result = np.maximum(result.astype(float)/np.iinfo(dtype).max, -1)
        return result

    @staticmethod
    def transform(node):
        if 'matrix' in node:
            return np.array(node['matrix']).reshape(4, 4).T
        x, y, z, w = node.get('rotation', [0, 0, 0, 1])
        rotation = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                             [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                             [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        m = np.eye(4)
        m[:3, :3] = rotation @ np.diag(node.get('scale', [1, 1, 1]))
        m[:3, 3] = node.get('translation', [0, 0, 0])
        return m

    def parts(self):
        parts = []

        def visit(index, parent, ancestry):
            node = self.doc['nodes'][index]
            world = parent @ self.transform(node)
            ancestry = ancestry + [node.get('name', str(index))]
            if 'mesh' in node:
                for primitive in self.doc['meshes'][node['mesh']]['primitives']:
                    assert primitive.get('mode', 4) == 4
                    if 'KHR_draco_mesh_compression' in primitive.get('extensions', {}):
                        import DracoPy
                        draco = primitive['extensions']['KHR_draco_mesh_compression']
                        decoded = DracoPy.decode(self.view(draco['bufferView']))
                        attrs = {name: decoded.get_attribute_by_unique_id(uid)['data']
                                 for name, uid in draco['attributes'].items()}
                        indices = decoded.faces.reshape(-1).astype('<u4')
                    else:
                        attrs = {name: self.accessor(a) for name, a in primitive['attributes'].items()}
                        indices = (self.accessor(primitive['indices']).reshape(-1).astype('<u4')
                                   if 'indices' in primitive else np.arange(len(attrs['POSITION']), dtype='<u4'))
                    p = np.c_[attrs['POSITION'], np.ones(len(attrs['POSITION']))] @ world.T
                    n = attrs['NORMAL'] @ np.linalg.inv(world[:3, :3])
                    n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-10)
                    if np.linalg.det(world[:3, :3]) < 0:
                        indices = indices.reshape(-1, 3)[:, ::-1].reshape(-1).copy()
                    parts.append(dict(name='/'.join(ancestry), ancestors=ancestry,
                                      p=p[:, :3], n=n, uv=attrs.get('TEXCOORD_0', np.zeros((len(p), 2))),
                                      material=primitive.get('material', 0), indices=indices))
            for child in node.get('children', []):
                visit(child, world, ancestry)

        for root in self.doc['scenes'][self.doc.get('scene', 0)]['nodes']:
            visit(root, np.eye(4), [])
        return parts

    def image(self, texture):
        img = self.doc['images'][self.doc['textures'][texture]['source']]
        data = (self.source.parent / img['uri']).read_bytes() if 'uri' in img else self.view(img['bufferView'])
        return Image.open(io.BytesIO(data)).convert('RGBA')


def inspect(source):
    gltf = Gltf(source)
    parts = gltf.parts()
    print('Asset:', gltf.doc['asset'])
    for i, material in enumerate(gltf.doc['materials']):
        print('MATERIAL', i, material.get('name'), material.get('pbrMetallicRoughness'))
    for p in parts:
        print('PART', p['name'], 'mat', p['material'], 'triangles', len(p['indices'])//3,
              'bounds', np.round(p['p'].min(axis=0), 3), np.round(p['p'].max(axis=0), 3))


PROFILES = {
    'supra': dict(length=4.52, reverse=False, wheels=['wheel', 'wheel.001', 'wheel.002', 'wheel.003'], tyre=8,
                  paint=5, colour=[.78, .79, .76, 1]),
    'ferrari': dict(length=4.53, reverse=True, wheels=['wheel_rr', 'wheel_rl', 'wheel_fl', 'wheel_fr'], tyre=8,
                    paint=14, colour=[.64, .008, .014, 1]),
    'corvette': dict(length=4.49, reverse=False, wheels=['Rim FL', 'Rim FR', 'Rim RL', 'Rim RR'], tyre=4,
                     paint=29, colour=[.015, .075, .28, 1]),
}


def convert(source, destination, profile):
    gltf = Gltf(source)
    parts = gltf.parts()
    cfg = PROFILES[profile]
    # glTF is right handed; the game's +Z-forward projection has +X to the
    # driver's right. Change handedness as well as forward direction, otherwise
    # badges/plates and left/right interior details appear mirrored in-game.
    rotation = np.diag([1, 1, -1]) if cfg['reverse'] else np.diag([-1, 1, 1])
    for p in parts:
        p['p'] = p['p'] @ rotation
        p['n'] = p['n'] @ rotation
        p['indices'] = p['indices'].reshape(-1, 3)[:, ::-1].reshape(-1).copy()
    bounds = np.concatenate([p['p'] for p in parts])
    scale = cfg['length'] / np.ptp(bounds[:, 2])
    offset = np.array([-(bounds[:, 0].min()+bounds[:, 0].max())/2, -bounds[:, 1].min(), 0]) * scale
    for p in parts:
        p['p'] = p['p']*scale+offset
    centers = np.zeros((5, 4), dtype='<f4')
    wheel_ids = {}
    for group in cfg['wheels']:
        tyres = np.concatenate([p['p'] for p in parts if group in p['ancestors'] and p['material'] == cfg['tyre']])
        low, high = tyres.min(axis=0), tyres.max(axis=0)
        c = (low+high)/2
        wheel = (1 if c[0] < 0 else 2) if c[2] > 0 else (3 if c[0] < 0 else 4)
        centers[wheel] = [*c, (high[1]-low[1])/2]
        wheel_ids[group] = wheel
    assert len(set(wheel_ids.values())) == 4
    shift = 1.45-float(centers[1:3, 2].mean())
    centers[1:, 2] += shift
    for p in parts:
        p['p'][:, 2] += shift
    assert np.all((centers[1:, 3] > .25) & (centers[1:, 3] < .45)), centers

    destination.mkdir(parents=True, exist_ok=True)
    textures = {}
    materials = []
    for i, m in enumerate(gltf.doc['materials']):
        pbr = m.get('pbrMetallicRoughness', {})
        base = pbr.get('baseColorFactor', [1, 1, 1, 1]).copy()
        metal, rough = pbr.get('metallicFactor', 1), max(.06, pbr.get('roughnessFactor', 1))
        coat = glass = brake = 0
        emission = m.get('emissiveFactor', [0, 0, 0]).copy()
        texture = pbr.get('baseColorTexture', {}).get('index')
        layer = -1
        if texture is not None:
            if texture not in textures:
                textures[texture] = len(textures)
                gltf.image(texture).resize((512, 512), Image.Resampling.LANCZOS).save(
                    destination / f'texture_{textures[texture]}.png')
            layer = textures[texture]
        if i == cfg['paint']:
            base, metal, rough, coat = cfg['colour'], .35, .19, 1
        if profile == 'supra':
            if i == 1:
                base, metal, rough, glass = [.03, .05, .06, 1], 0, .08, 1
            if i == 4:
                metal, rough = .95, .08
            if i == 8:
                metal, rough = 0, .85
            if i in (9, 10):
                metal, rough = .8, .25
        elif profile == 'ferrari':
            if i == 1:
                base, rough, brake = [.35, .004, .01, 1], .2, 1
            if i == 3:
                base, metal, rough = [.35, .36, .38, 1], .85, .24
            if i == 4:
                base, rough = [.7, .75, .8, .25], .08
            if i == 8:
                base, metal, rough = [.008, .009, .01, 1], 0, .9
            if i == 9:
                base, metal, rough, glass = [.03, .05, .06, 1], 0, .08, 1
            if i == 10:
                metal, rough = .95, .12
            if i in (11, 12, 13):
                base, metal, rough = [.02, .022, .024, 1], .1, .65
        else:
            if i == 0:
                base, metal, rough = [.24, .25, .27, 1], .85, .24
            if i == 24:
                base, metal, rough, glass = [.03, .05, .06, 1], 0, .08, 1
            if i in (17, 19):
                base, metal, rough, brake = [.36, .003, .008, 1], .15, .18, 1
            if i in (18, 20):
                emission = [.3, .34, .4]
            if i in (6, 27, 30):
                rough, coat = .28, .4
        # ESC2 adds one float4 texture descriptor to the ESC1 material fields.
        materials.append([*base, metal, rough, coat, glass, *emission, brake, layer, 0, 0, 0])

    dtype = [('p', '<f4', 3), ('n', '<f4', 3), ('uv', '<f4', 2), ('material', '<u4'), ('wheel', '<u4')]
    vertices, primitives, count = [], [], 0
    for part in parts:
        material = part['material']
        name = part['name'].lower()
        if profile == 'supra' and material == 1 and any(t in name for t in ('light', 'airdam', 'body_4w')):
            lens = materials[material].copy()
            lens[:4], lens[7] = [.75, .8, .85, .12], 0
            material = len(materials)
            materials.append(lens)
        if profile == 'supra' and part['material'] == 3 and part['p'][:, 2].mean() < 0:
            lamp = materials[material].copy()
            lamp[11] = 2  # Illuminate red texels of the tail lamps, not white reversing lights.
            material = len(materials)
            materials.append(lamp)
        if profile == 'ferrari' and 'steering_' in name and material == 1:
            lamp = materials[material].copy()
            lamp[11] = 0
            material = len(materials)
            materials.append(lamp)
        indices = part['indices']
        if profile == 'corvette' and len(indices) > 6000:
            import meshoptimizer
            reduced = np.empty_like(indices)
            actual = meshoptimizer.simplify(reduced, indices, np.asarray(part['p'], dtype='<f4'),
                                             target_index_count=int(len(indices)*.3)//3*3, target_error=.003)
            indices = reduced[:actual]
        # Compact vertices after simplification; preserve each referenced UV/normal.
        used, indices = np.unique(indices, return_inverse=True)
        v = np.zeros(len(used), dtype=dtype)
        v['p'], v['n'], v['uv'], v['material'] = part['p'][used], part['n'][used], part['uv'][used], material
        for group, wheel in wheel_ids.items():
            if group in part['ancestors']:
                v['wheel'] = wheel | (8 if profile == 'supra' and part['material'] == 11 else 0)
        if profile == 'corvette' and 'brake caliper' in name:
            center = part['p'].mean(axis=0)
            v['wheel'] = int(np.argmin(np.linalg.norm(centers[1:, :3]-center, axis=1)))+1 | 8
        vertices.append(v)
        primitives.append((material, indices.astype('<u4')+count))
        count += len(v)
    vertices = np.concatenate(vertices)
    indices = np.concatenate([p[1] for p in sorted(primitives, key=lambda p: materials[p[0]][3] < .99)])
    assert len(materials) <= 64 and len(textures) <= 32
    assert np.isfinite(vertices['p']).all() and np.isfinite(vertices['n']).all()
    with (destination/'car.mesh').open('wb') as f:
        f.write(struct.pack('<4s4I', b'ESC2', len(vertices), len(indices), len(materials), 5))
        f.write(centers.tobytes())
        f.write(np.asarray(materials, dtype='<f4').tobytes())
        f.write(vertices.tobytes())
        f.write(indices.astype('<u4').tobytes())
    report = dict(vertices=len(vertices), triangles=len(indices)//3, materials=len(materials), texture_layers=len(textures),
                  bounds=[vertices['p'].min(axis=0).tolist(), vertices['p'].max(axis=0).tolist()], wheels=centers.tolist())
    (destination/'mesh-info.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--inspect', action='store_true')
    parser.add_argument('--profile', choices=PROFILES)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.inspect:
        inspect(args.source)
    else:
        if not args.profile or not args.output:
            parser.error('Conversion needs --profile and --output')
        convert(args.source, args.output, args.profile)

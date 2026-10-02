#!/usr/bin/env python3
"""Bake the credited Khronos CarConcept GLB into our compact Metal mesh.

Usage: python3 tools/import_concept_car.py INPUT.glb assets/vehicles/concept
Requires numpy only at import time, never at build/runtime. Retains authored
normals/material factors; straightens the front wheels and preserves pivots.
Textures, logos, animations and glTF transmission/iridescence are not imported.
"""
import json
import struct
import sys
from pathlib import Path
import numpy as np

source, destination = Path(sys.argv[1]), Path(sys.argv[2])
data = source.read_bytes()
assert data[:4] == b'glTF'
length, _ = struct.unpack_from('<II', data, 12)
gltf = json.loads(data[20:20+length])
binary = data[28+length:]
destination.mkdir(parents=True, exist_ok=True)

def accessor(index):
    a = gltf['accessors'][index]
    b = gltf['bufferViews'][a['bufferView']]
    dtype = np.dtype({5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']])
    columns = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    result = np.ndarray((a['count'], columns), dtype, buffer=binary,
        offset=b.get('byteOffset',0)+a.get('byteOffset',0),
        strides=(b.get('byteStride',columns*dtype.itemsize),dtype.itemsize)).copy()
    if a.get('normalized'):
        result=np.maximum(result.astype(float)/np.iinfo(dtype).max,-1)
    return result

def transform(node):
    if 'matrix' in node:return np.array(node['matrix']).reshape(4,4).T
    x,y,z,w=node.get('rotation',[0,0,0,1])
    r=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
        [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
        [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    result=np.eye(4);result[:3,:3]=r@np.diag(node.get('scale',[1,1,1]));result[:3,3]=node.get('translation',[0,0,0]);return result

vertices, primitives = [], []
centers=[[0,0,0,0]]
vertex_count=0
wheel_nodes={80:1,85:2,90:3,95:4}

def visit(index,parent,wheel=0,correction=None):
    global vertex_count
    node=gltf['nodes'][index];matrix=parent@transform(node)
    if index in wheel_nodes:
        wheel=wheel_nodes[index];center=matrix[:3,3]
        while len(centers)<=wheel:centers.append([0,0,0,0])
        centers[wheel]=[*center,0]
        angle=np.arctan2(matrix[2,0],matrix[0,0]);c,s=np.cos(angle),np.sin(angle)
        correction=np.eye(4);correction[:3,:3]=[[c,0,s],[0,1,0],[-s,0,c]]
        correction[:3,3]=center-correction[:3,:3]@center
    world=matrix if correction is None else correction@matrix
    if 'mesh' in node:
        for primitive in gltf['meshes'][node['mesh']]['primitives']:
            assert primitive.get('mode',4)==4
            attrs=primitive['attributes'];position=accessor(attrs['POSITION'])
            position=np.c_[position,np.ones(len(position))]@world.T
            normal=accessor(attrs['NORMAL'])@np.linalg.inv(world[:3,:3])
            normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-10)
            uv=accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(position),2))
            material=primitive.get('material',0)
            part=np.zeros(len(position),dtype=[('p','<f4',3),('n','<f4',3),('uv','<f4',2),('material','<u4'),('wheel','<u4')])
            part['p']=position[:,:3];part['n']=normal;part['uv']=uv;part['material']=material
            part['wheel']=0 if 'BrakePad' in node.get('name','') else wheel
            vertices.append(part)
            indices=accessor(primitive['indices']).reshape(-1).astype('<u4')+vertex_count
            if np.linalg.det(world[:3,:3])<0:indices=indices.reshape(-1,3)[:,::-1].reshape(-1).copy()
            primitives.append((material,indices));vertex_count+=len(position)
    for child in node.get('children',[]):visit(child,matrix,wheel,correction)

for root in gltf['scenes'][gltf.get('scene',0)]['nodes']:visit(root,np.eye(4))
materials=[]
for i,m in enumerate(gltf['materials']):
    p=m.get('pbrMetallicRoughness',{});extensions=m.get('extensions',{})
    base=p.get('baseColorFactor',[1,1,1,1])
    if i==1:base=[.035,.055,.065,.48]
    if i==18:base=[.025,.025,.025,1]
    materials.append([*base,p.get('metallicFactor',1),max(.06,p.get('roughnessFactor',1)),
        extensions.get('KHR_materials_clearcoat',{}).get('clearcoatFactor',0),float(i==1),
        *m.get('emissiveFactor',[0,0,0]),float(i==13)])
mesh=np.concatenate(vertices)
indices=np.concatenate([p[1] for p in sorted(primitives,key=lambda p:p[0]==1)])
with (destination/'car.mesh').open('wb') as out:
    out.write(struct.pack('<4s4I',b'ESC1',len(mesh),len(indices),len(materials),len(centers)))
    out.write(np.asarray(centers,dtype='<f4').tobytes())
    out.write(np.asarray(materials,dtype='<f4').tobytes())
    out.write(mesh.tobytes());out.write(indices.astype('<u4').tobytes())
print('vertices',len(mesh),'triangles',len(indices)//3,'bounds',mesh['p'].min(axis=0),mesh['p'].max(axis=0),'wheel centers',centers)

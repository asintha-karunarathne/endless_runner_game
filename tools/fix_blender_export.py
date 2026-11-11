from pygltflib import GLTF2
import struct, numpy as np
from scipy.spatial.transform import Rotation as R
SRC="/tmp/blender_player.glb"; DST="/home/claude/endless-runner/assets/models/player.glb"
g=GLTF2().load(SRC); blob=bytearray(g.binary_blob())
def read(i):
    a=g.accessors[i]; bv=g.bufferViews[a.bufferView]; n={"SCALAR":1,"VEC3":3,"VEC4":4}[a.type]
    o=(bv.byteOffset or 0)+(a.byteOffset or 0)
    return np.array(struct.unpack_from(f"<{a.count*n}f",blob,o),dtype=np.float64).reshape(a.count,n)
def write(i,arr):
    a=g.accessors[i]; bv=g.bufferViews[a.bufferView]; n={"SCALAR":1,"VEC3":3,"VEC4":4}[a.type]
    o=(bv.byteOffset or 0)+(a.byteOffset or 0)
    struct.pack_into(f"<{a.count*n}f",blob,o,*[float(v) for v in arr.flatten()])
    a.min=[float(v) for v in arr.min(axis=0)]; a.max=[float(v) for v in arr.max(axis=0)]
arm_idx=g.scenes[0].nodes[0]; arm=g.nodes[arm_idx]
S=arm.scale[0]; Rarm=R.from_quat(arm.rotation)
joints=list(g.skins[0].joints); jset=set(joints)
hips=next(j for j in joints if g.nodes[j].name=="mixamorig:Hips")
# raylib ignores non-joint ancestors when posing, so bake the armature's scale/rotation into the bones
for j in joints:
    n=g.nodes[j]; t=np.array(n.translation or [0,0,0],dtype=np.float64)*S; q=R.from_quat(n.rotation or [0,0,0,1])
    if j==hips: t=Rarm.apply(t); q=Rarm*q
    n.translation=[float(v) for v in t]; n.rotation=[float(v) for v in q.as_quat()]
done=set()
for anim in g.animations:
    for ch in anim.channels:
        if ch.target.node not in jset: continue
        acc=anim.samplers[ch.sampler].output
        if acc in done: continue
        done.add(acc); v=read(acc)
        if ch.target.path=="translation":
            v=v*S
            if ch.target.node==hips: v=Rarm.apply(v)
        elif ch.target.path=="rotation" and ch.target.node==hips:
            v=(Rarm*R.from_quat(v)).as_quat()
            for k in range(1,len(v)):
                if np.dot(v[k],v[k-1])<0: v[k]=-v[k]
        else: continue
        write(acc,v)
arm.rotation=None; arm.scale=None; arm.translation=None
mi=next(i for i,n in enumerate(g.nodes) if n.mesh is not None)
arm.children=[c for c in (arm.children or []) if c!=mi]
g.nodes[mi].translation=None; g.nodes[mi].rotation=None; g.nodes[mi].scale=None
g.scenes[0].nodes=[arm_idx,mi]
g.set_binary_blob(bytes(blob)); g.save(DST); print("saved",DST)

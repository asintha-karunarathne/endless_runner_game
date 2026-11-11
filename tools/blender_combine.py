import bpy, os, math
from mathutils import Matrix, Vector
bpy.ops.wm.read_factory_settings(use_empty=True)
anims_dir = "/tmp/inspect/anims/Animations"
files = [("Running.fbx","Run"),("Jumping.fbx","Jump"),("Idle.fbx","Idle"),("Walking.fbx","Walk")]
main_arm = None
for fname, label in files:
    before = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=os.path.join(anims_dir, fname))
    new = [bpy.data.objects[n] for n in set(bpy.data.objects.keys()) - before]
    arm = next(o for o in new if o.type == 'ARMATURE')
    mesh = next((o for o in new if o.type == 'MESH'), None)
    act = arm.animation_data.action; act.name = label; act.use_fake_user = True
    if main_arm is None: main_arm = arm
    else:
        bpy.data.objects.remove(arm, do_unlink=True)
        if mesh: bpy.data.objects.remove(mesh, do_unlink=True)

# ---------------- hand-posed actions (no clips were supplied for these) ----------------
arm = main_arm
P = dict(BACK=float(os.environ.get("BACK", 72)), HIPY=float(os.environ.get("HIPY", 20)),
         KNEE=float(os.environ.get("KNEE", 18)), ARM_OUT=float(os.environ.get("ARM_OUT", 55)),
         ARM_FWD=float(os.environ.get("ARM_FWD", 70)), ELBOW=float(os.environ.get("ELBOW", 25)), HEAD=float(os.environ.get("HEAD", 60)))
pbs = arm.pose.bones
for pb in pbs: pb.rotation_mode = 'QUATERNION'

def reset():
    for pb in pbs:
        pb.location = (0,0,0); pb.rotation_quaternion = (1,0,0,0); pb.scale = (1,1,1)
    bpy.context.view_layer.update()

def rot_about(name, axis, deg):          # rotate a bone (and its children) about its own head, in armature space
    pb = pbs["mixamorig:" + name]; h = Vector(pb.head)
    R = Matrix.Rotation(math.radians(deg), 4, axis)
    pb.matrix = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ pb.matrix
    bpy.context.view_layer.update()

def make_action(name, build):
    act = bpy.data.actions.new(name); act.use_fake_user = True
    arm.animation_data.action = act
    reset(); build()
    for f in (1, 12):
        for pb in pbs:
            pb.keyframe_insert("location", frame=f); pb.keyframe_insert("rotation_quaternion", frame=f); pb.keyframe_insert("scale", frame=f)

def pose_slide():
    rot_about("LeftArm", 'Z', -P["ARM_OUT"]);  rot_about("RightArm", 'Z', P["ARM_OUT"])
    rot_about("LeftArm", 'X', P["ARM_FWD"]);   rot_about("RightArm", 'X', P["ARM_FWD"])
    rot_about("LeftForeArm", 'X', P["ELBOW"]); rot_about("RightForeArm", 'X', P["ELBOW"])
    rot_about("Neck", 'X', P["HEAD"] * 0.4);   rot_about("Head", 'X', P["HEAD"] * 0.6)
    rot_about("LeftLeg", 'X', P["KNEE"]);      rot_about("RightLeg", 'X', P["KNEE"])
    rot_about("Hips", 'X', -P["BACK"])
    hips = pbs["mixamorig:Hips"]; m = hips.matrix.copy(); m.translation.y = P["HIPY"]; hips.matrix = m
    bpy.context.view_layer.update()

# the runner, caught: standing, bowed, hands held behind the back
A = dict(ARMDOWN=float(os.environ.get("A_DOWN", 85)), ARMBACK=float(os.environ.get("A_BACK", 5)), ELB=float(os.environ.get("A_ELB", 20)),
         IN=float(os.environ.get("A_IN", 70)), BOW=float(os.environ.get("A_BOW", 14)), HEADD=float(os.environ.get("A_HEAD", 28)))
def pose_arrested():
    for side, sgn in (("Left", 1), ("Right", -1)):
        rot_about(side + "Arm", 'Z', -sgn * A["ARMDOWN"])
        rot_about(side + "Arm", 'X', A["ARMBACK"])
        rot_about(side + "ForeArm", 'X', A["ELB"])
        rot_about(side + "ForeArm", 'Z', -sgn * A["IN"])
    rot_about("Spine", 'X', A["BOW"]); rot_about("Spine1", 'X', A["BOW"] * 0.6)
    rot_about("Neck", 'X', A["HEADD"] * 0.4); rot_about("Head", 'X', A["HEADD"] * 0.6)

# the policeman, making the arrest: leaning in, both arms reaching forward-down to cuff
G = dict(LEAN=float(os.environ.get("G_LEAN", 10)), REACH=float(os.environ.get("G_REACH", 8)), ELB=float(os.environ.get("G_ELB", 10)),
         HEAD=float(os.environ.get("G_HEAD", -6)), IN=float(os.environ.get("G_IN", 35)))
def pose_grab():
    rot_about("LeftArm", 'Y', -90);  rot_about("RightArm", 'Y', 90)
    for side in ("Left", "Right"):
        rot_about(side + "Arm", 'X', G["REACH"])
        rot_about(side + "ForeArm", 'X', G["ELB"])
        rot_about(side + "ForeArm", 'Y', -G["IN"] if side == "Left" else G["IN"])
    rot_about("Spine", 'X', G["LEAN"]); rot_about("Spine1", 'X', G["LEAN"] * 0.5)
    rot_about("Head", 'X', G["HEAD"])

make_action("Slide", pose_slide)
make_action("Arrested", pose_arrested)
make_action("Grab", pose_grab)
if os.environ.get("NOEXPORT"): raise SystemExit
print("Actions:", [a.name for a in bpy.data.actions])
bpy.ops.export_scene.gltf(filepath="/tmp/blender_player.glb", export_format='GLB', export_animations=True,
                          export_animation_mode='ACTIONS', export_bake_animation=False, use_selection=False)

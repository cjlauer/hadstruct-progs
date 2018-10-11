import xml.etree.ElementTree as ET
from xml.dom import minidom
import sys
import re

L = 32
T = 64
procs = 4,4,4,8

mu = 0.003
csw = 1.74
kappa = 0.1400645

n_ape = 50
alpha_ape = 0.5
n_gauss = 90
alpha_gauss = 0.2

multigrid = dict(verbosity = 2,
                 block = (4,4,4,4),
                 level_1 = dict(n_basis = 24,
                                mu = mu,
                                setup_iters = 5),
                 level_2 = dict(n_basis = 24,
                                mu = mu,
                                setup_iters = 5) )

def get_spos(repl, traj, spos_idx):
    spos = []
    for line in open("sources-%s.list" % repl).readlines():
        if line.split(":")[0].strip() == traj:
            ss = line.split(":")[1].split()
            ss = [ss[i] for i in spos_idx]
            for s in ss:
                m = re.search("sx([0-9]*)sy([0-9]*)sz([0-9]*)st([0-9]*)", s)
                sx,sy,sz,st = tuple(map(int, m.groups()))
                spos.append(tuple([st, sx, sy, sz]))
            return spos

def get_sinks():
    sinks = []
    for dt in 12,14:
        for pr in "P0",:
           sinks.append(tuple([int(dt),pr]))
    return sinks

repl = sys.argv[1]
traj = sys.argv[2]
spos = sys.argv[3]

spos_idx = tuple(list(map(int, spos.split(","))))

conf_dir = "/lcrc/project/pieiclat/clauer/cA211.30.32/Confs/"
corr_dir = "/lcrc/project/pieiclat/clauer/cA211.30.32/Corr/%s-%s" % (repl, traj)
prop_dir = "/lcrc/project/pieiclat/clauer/cA211.30.32/Props/%s-%s" % (repl, traj)

spos = get_spos(repl, traj, spos_idx)
sources = [dict(coords = sp) for sp in spos]

for i,s in enumerate(sources):
    sinks = get_sinks()
    sources[i]["sinks"] = sinks

tree = ET.Element("hadstruct-input")
ET.SubElement(tree, "dims").text = "%d %d %d %d" % (T, L, L, L)
ET.SubElement(tree, "procs").text = "%d %d %d %d" % tuple(procs)
ET.SubElement(tree, "config").text = conf_dir + ("conf.%s-%04d" % (repl, int(traj)))
ET.SubElement(tree, "corrs-dir").text = corr_dir
ET.SubElement(tree, "props-dir").text = prop_dir
s = ET.SubElement(tree, "smearing")
ET.SubElement(s, "n_ape").text = str(n_ape)
ET.SubElement(s, "alpha_ape").text = str(alpha_ape)
ET.SubElement(s, "n_gauss").text = str(n_gauss)
ET.SubElement(s, "alpha_gauss").text = str(alpha_gauss)
a = ET.SubElement(tree, "action")
ET.SubElement(a, "mu").text = str(mu)
ET.SubElement(a, "kappa").text = str(kappa)
ET.SubElement(a, "csw").text = str(csw)
mg = ET.SubElement(tree, "multi-grid")
ET.SubElement(mg, "verbosity").text = str(multigrid["verbosity"])
ET.SubElement(mg, "block").text = "%d %d %d %d" % tuple(multigrid["block"])
nl = len(list(filter(lambda x: "level_" in x, multigrid.keys())))
ET.SubElement(mg, "n_levels").text = str(nl)
for i in range(nl):
    s = "level_%d" % (i+1)
    mgl = ET.SubElement(mg, s)
    ET.SubElement(mgl, "setup_iters").text = str(multigrid[s]["setup_iters"])
    ET.SubElement(mgl, "n_basis_vectors").text = str(multigrid[s]["n_basis"])
    ET.SubElement(mgl, "mu").text = str(multigrid[s]["mu"])

for so in sources:
    sp = ET.SubElement(tree, "sp")
    co = ET.SubElement(sp, "coords")
    co.text = "%d %d %d %d" % so["coords"]
    for si in so.get("sinks", []):
        sk = ET.SubElement(sp, "sink")
        pr = ET.SubElement(sk, "proj")
        pr.text = si[1]
        dt = ET.SubElement(sk, "dt")
        dt.text = str(si[0])

flat = ET.tostring(tree, "utf-8")
frmt = minidom.parseString(flat)
print(frmt.toprettyxml(indent = "   "))

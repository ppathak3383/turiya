# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - runs the proof against its vacuity checks and its mutants: each mutant must FAIL, each K must VERIFY.
# Usage: python3 run_kosa_mutants.py [organs folder]   (default: the folder above this one)
import subprocess, shutil, os, sys, tempfile
HERE=os.path.dirname(os.path.abspath(__file__))
ORG=sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(HERE)
H=open(ORG+'/kosa_cmd.h').read()
def run(theorem, hdr=None, extra=()):
    d=tempfile.mkdtemp()
    open(d+'/kosa_cmd.h','w').write(hdr if hdr is not None else H)
    shutil.copy(ORG+'/kosa_window.h', d)
    cmd=['cbmc',os.path.join(HERE,'kosa_proof.cc'),'-I'+d,'-DTHEOREM_'+theorem,'--unwind','2',*extra]
    out=subprocess.run(cmd,capture_output=True,text=True,timeout=900).stdout
    shutil.rmtree(d, ignore_errors=True)
    ok='VERIFICATION SUCCESSFUL' in out; fail='VERIFICATION FAILED' in out
    why=[l.strip() for l in out.splitlines() if 'FAILURE' in l][:2]
    return 'VERIFIED' if ok else ('FAILED' if fail else 'ERROR '+out[-300:]), why
def mut(old,new):
    assert old in H, old
    return H.replace(old,new,1)
cases=[
 ('vacuity A (must fail)','A',None,('-DVACUITY',)),
 ('vacuity B (must fail)','B',None,('-DVACUITY',)),
 ('M1 the code before tonight (32-bit overflow)','B',mut(
   "if (bs < 512 || bs > 4096 || !count || count > 4096 / bs || lba >= W->blocks || count > W->blocks - lba) return 8;",
   "if (!count || count * W->block_size > 4096 || lba + count > W->blocks) return 8;").replace(
   "return opc == 0x02 && s.prp1 == W->pa[KOSA_DATA] && (s.cdw12 >> 16) == 0\n\t\t    && bs >= 512 && bs <= 4096 && (s.cdw12 & 0xffff) < 4096 / bs;   /* blocks fit the page */",
   "(void)bs; return opc == 0x02;"),()),
 ('M2 page check off by one','A',mut("(s.cdw12 & 0xffff) < 4096 / bs","(s.cdw12 & 0xffff) <= 4096 / bs"),()),
 ('M3 doorbell releases two entries','A',mut("q.tail = (q.tail + 1) % QN;","q.tail = (q.tail + 2) % QN;"),()),
 ('M4 admin accepts Set Features (0x09)','A',mut("\tif (opc == 0x06)","\tif (opc == 0x09) return true;\n\tif (opc == 0x06)"),()),
 ('M5 fused commands let through','A',mut("if ((s.cdw0 >> 8) & 0xff) return false;","").replace("s->cdw0 = (e.cdw0 & 0xff) |","s->cdw0 = (e.cdw0 & 0xffff) |"),()),
 ('M6 last block plus one','B',mut("lba >= W->blocks || count > W->blocks - lba","lba > W->blocks || count > W->blocks - lba + 1"),()),
 ('M7 Create CQ without physically contiguous','A',mut("(s.cdw10 >> 16) < QN && (s.cdw11 & 1);\n\tif (opc == 0x01)","(s.cdw10 >> 16) < QN;\n\tif (opc == 0x01)"),()),
 ('K1 first check removed: the second alone holds','A',mut("if (!allowed(q.qid, e)) fall();","")
   ,()),
 ('K2 second check removed: the first alone holds','A',mut("if (!allowed(q.qid, snapshot(s))) fall();",""),()),
]
for name,th,hdr,extra in cases:
    r,why=run(th,hdr,extra)
    print(f"{name:52s} {r:9s} {why[0] if why else ''}")
    sys.stdout.flush()

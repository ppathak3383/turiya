# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - runs the proof against its vacuity checks and its mutants: each mutant must FAIL, each K must VERIFY.
# Usage: python3 run_nadi_mutants.py [organs folder]   (default: the folder above this one)
import subprocess, shutil, os, sys, tempfile
HERE=os.path.dirname(os.path.abspath(__file__))
ORG=sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(HERE)
H=open(ORG+'/nadi_scsi.h').read()
def run(theorem, hdr=None, extra=()):
    d=tempfile.mkdtemp()
    open(d+'/nadi_scsi.h','w').write(hdr if hdr is not None else H)
    shutil.copy(ORG+'/nadi_window.h', d)
    cmd=['cbmc',os.path.join(HERE,'nadi_proof.cc'),'-I'+d,'-DTHEOREM_'+theorem,'--unwind','33','--unwinding-assertions',*extra]
    out=subprocess.run(cmd,capture_output=True,text=True,timeout=900).stdout
    shutil.rmtree(d, ignore_errors=True)
    ok='VERIFICATION SUCCESSFUL' in out; fail='VERIFICATION FAILED' in out
    why=[l.strip() for l in out.splitlines() if 'FAILURE' in l][:1]
    return 'VERIFIED' if ok else ('FAILED' if fail else 'ERROR '+out[-300:]), why
def mut(old,new,base=None):
    b=base if base is not None else H
    assert old in b, old
    return b.replace(old,new,1)
OLD_READ=mut("""	if (!W->storage_slot || bs < 512 || bs > 4096 || !count || count > 4096 / bs
	    || lba >= W->storage_blocks || count > W->storage_blocks - lba || lba + count - 1 > 0xffffffffULL)
		return 8;""","""	if (!W->storage_slot || !count || count * W->storage_block_size > 4096 || lba + count > W->storage_blocks || lba > 0xffffffffULL)
		return 8;""")
OLD_READ=mut("""		if (n != 10 || bs < 512 || bs > 4096 || !blocks || blocks > 4096 / bs || blocks * bs != inlen) return false;""",
             """		(void)bs; (void)blocks;""", OLD_READ)
cases=[
 ('vacuity C (must fail)','C',None,('-DVACUITY',)),
 ('vacuity D (must fail)','D',None,('-DVACUITY',)),
 ('N1 the code before tonight (32-bit overflow)','D',OLD_READ,()),
 ('N2 WRITE(10) let through','C',mut("op == 0x25 || op == 0x28; }","op == 0x25 || op == 0x28 || op == 0x2a; }"),()),
 ('N3 direction OUT with data','C',mut("if (flags != (inlen ? 0x80 : 0x00)) return false;","").replace("cbw[12] = inlen ? 0x80 : 0;","cbw[12] = 0;"),()),
 ('N4 READ length not matched to the page','C',mut("|| blocks * bs != inlen) return false;",") return false;"),()),
 ('N5 no end-of-stick check','D',mut("|| lba >= W->storage_blocks || count > W->storage_blocks - lba ",""),()),
 ('N6 more than a page','C',mut("|| inlen > 4096) return false;","|| inlen > 8192) return false;"),()),
 ('K3 first check removed: the second alone holds','C',mut("if (!cbw_allowed(cdb, cdblen, inlen, inlen ? 0x80 : 0x00)) fall();","if (cdblen > 16) fall();"),()),
 ('K4 second check removed: the first alone holds','C',mut("if (said != inlen || !cbw_allowed(cbw + 15, cbw[14], said, cbw[12])) fall();",""),()),
]
for name,th,hdr,extra in cases:
    r,why=run(th,hdr,extra)
    print(f"{name:52s} {r:9s} {why[0] if why else ''}")
    sys.stdout.flush()

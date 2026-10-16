# simulate wpn state machine for switchfrom/switchto, delaystart=ds, delayend=de
import sys
ds=int(sys.argv[1]) if len(sys.argv)>1 else 1
de=int(sys.argv[2]) if len(sys.argv)>2 else 1
class W:
    def __init__(s,name):
        s.name=name; s.counter=0; s.state=0; s.next=0; s.prev=0; s.phase=4; s.deploy=0
IDLE,EI,FIRE,REC,REL,EMP,TO,FROM,RANK=range(9)
A=W('A'); B=W('B'); B.deploy=-901; B.state=FROM; B.next=0
cur=A; pending=None; log=[]
def generic(w):
    if w.phase&1: w.phase=2
    elif w.phase&0x40: w.phase=2
def h_from(w,t):
    global cur,pending
    generic(w)
    if w.counter==0 and w.phase not in (4,0x40):
        w.deploy=0; w.counter=de; w.next=w.prev; w.phase=4; log.append((t,'from: first-branch reset phase=4')); return
    if w.deploy>=-900:
        w.deploy-=30; w.counter=de
    else:
        w.deploy=0; w.counter=0; w.phase=4
        cur=pending; pending=None
        n=cur
        if n.state!=TO:
            n.next = TO if n.phase in (4,0) else 0
        w.deploy=-901
        log.append((t,'COMMIT -> B current, B.next=%d'%n.next))
def h_to(w,t):
    generic(w)
    if w.counter==0 and w.phase not in (4,0x40):
        w.deploy=-900; w.counter=de; w.next=w.prev; w.phase=4; log.append((t,'to: first-branch')); return
    if w.deploy>0:
        w.deploy=0; w.counter=0; w.phase=4; log.append((t,'to: DONE (deploy>0)'))
    else:
        w.deploy+=30; w.counter=de
def h_idle(w,t):
    generic(w)
def handler(w,t):
    {FROM:h_from,TO:h_to}.get(w.state,h_idle)(w,t)
def tick(t):
    w=cur
    if w.counter>0:
        w.counter-=1; handler(w,t)
    else:
        if w.state==w.next: w.counter-=1; handler(w,t)
        else:
            if w.phase in (4,0):
                if w.state not in (TO,FROM): w.prev=w.state
                w.state=w.next; w.next=0
                w.counter={TO:ds,FROM:ds}.get(w.state,0); w.phase=1
                log.append((t,'start state %d on %s'%(w.state,w.name)))
                handler(w,t)
            else:
                w.counter-=1; handler(w,t)
# request at t=0
A.state=0;A.next=FROM;A.counter=0;A.phase=4;A.deploy=0
pending=B
for t in range(1,90):
    tick(t)
    if cur is B and B.state==IDLE and B.phase in(4,0) and B.counter<=0 and t>1: log.append((t,'B idle ready')); break
for l in log: print(l)

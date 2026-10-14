import sys, struct, math, array
path, ch = sys.argv[1], int(sys.argv[2])
data = array.array("f")
with open(path, "rb") as f:
    data.frombytes(f.read())
n = len(data) // ch
print(f"channels={ch} frames={n} seconds={n/48000:.2f} (rate assumed 48000)")
for c in range(ch):
    seg = data[c::ch]
    sq = sum(x * x for x in seg)
    peak = max((abs(x) for x in seg), default=0.0)
    rms = math.sqrt(sq / len(seg)) if seg else 0.0
    db = 20 * math.log10(rms) if rms > 0 else float("-inf")
    pk = 20 * math.log10(peak) if peak > 0 else float("-inf")
    print(f"ch{c+1}: rms={db:7.1f} dBFS  peak={pk:7.1f} dBFS")

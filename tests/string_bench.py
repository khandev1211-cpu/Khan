import time
start = time.time()
s = ""
for i in range(10000):
    s += "a"
end = time.time()
print(f"Python String Time: {end - start:.6f}s")

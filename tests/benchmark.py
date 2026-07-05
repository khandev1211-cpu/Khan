import time

def is_prime(n):
    if n < 2:
        return False
    i = 2
    while i * i <= n:
        if n % i == 0:
            return False
        i += 1
    return True

start = time.time()
count = 0
n = 2
while n < 50000:
    if is_prime(n):
        count += 1
    n += 1
end = time.time()

print(f"Python found {count} primes.")
print(f"Python Time: {end - start:.4f}s")

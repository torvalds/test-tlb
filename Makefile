test-tlb: test-tlb.c
	gcc -Wall -O test-tlb.c -o test-tlb -lm

run: test-tlb
	for i in 4k 8k 16k 32k 64k 128k 256k 512k 1M 2M 4M 8M 16M 32M 64M 128M ; do echo -n "$$i: "; ./test-tlb $$i 4k; done

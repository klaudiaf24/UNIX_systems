
all:
	clang++ -std=c++17 main.cpp -g -o a.out -lrt 
clean:
	rm -rf a.out a.out.dSYM

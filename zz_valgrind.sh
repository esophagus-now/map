clang -g -o dbg *.c
valgrind --leak-check=full -v ./dbg <test.txt 2>report.txt
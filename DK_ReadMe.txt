windres diary.rc -O coff -o diary.res
gcc diary.c diary.res -o diary.exe
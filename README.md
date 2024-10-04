Multithreaded HTTP REST API for PostgreSQL in C for UNIX and Windows
for 2813ICT university course
'Forest Health Project'

OpenSSL, picohttpparser, tiny-json, libpq

Build and run instructions
Windows with MinGW:
 - install openssl and libpq dependencies
 - cd into root folder
 - run 'mingw32-make server'
 - run '.\bin\server <thread count>'

tests/ instructions
integration.c - server load testing
 - run .\bin\server <thread count>
 - gcc compile integration.c (windows requires lws2_32 dependency)
 - run compiled executable '.\a.exe <thread count> <socket count> <packet count>'

time.c - server latency testing
 - run .\bin\server <thread count>
 - gcc compile time.c (windows requires lws2_32 dependency)
 - run compiled executable '.\a.exe <packet count>'



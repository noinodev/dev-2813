@echo off
setlocal

REM Number of requests to send
set num_requests=1

REM PowerShell command to send requests concurrently
for /L %%i in (1,1,%num_requests%) do (
    start /b powershell -Command '"curl -X POST http://localhost:8888/accounts/login -H "Content-Type: application/json" -d {\"username\":\"user\",\"password\":\"pass\",\"email\":\"user@example.com\"}"'
)

echo All requests sent!
pause
param(
    [string]$Compiler = "clang++"
)

& $Compiler -std=c++17 -Wall -Wextra -Wpedantic -O2 lexer.cpp -o thc.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }


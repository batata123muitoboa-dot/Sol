@echo off

echo ONLY AVAILABLE IN PORTUGUESE!! -- APENAS DISPONIVEL EM PORTUGUES!!
timeout /t 1 /nobreak >nul

if exist Solua (
    echo A pasta Solua ja existe.
    cd Solua
) else (
    git clone https://github.com/batata123muitoboa-dot/Solua.git
    if errorlevel 1 (
        echo Erro ao clonar o repositorio.
        pause
        exit /b 1
    )
    cd Solua
)

echo.
echo Solua instalado em %cd%.
echo.

set /p RESPOSTA=Fazer Visual Studio Code suportar Solua? [S/n] 

if "%RESPOSTA%"=="" set RESPOSTA=S

if /i "%RESPOSTA%"=="S" goto vscode
if /i "%RESPOSTA%"=="N" goto fim

echo Resposta invalida. Suporte do VS Code ignorado.
goto fim

:vscode

echo.
echo Instalando suporte da Solua no VS Code...

where npm >nul 2>&1
if errorlevel 1 (
    echo.
    echo npm nao esta instalado.
    echo Instale Node.js e npm.
    goto fim
)

where vsce >nul 2>&1
if errorlevel 1 (
    echo vsce nao encontrado.
    echo Instalando @vscode/vsce...
    call npm install -g @vscode/vsce
    if errorlevel 1 (
        echo.
        echo ERRO: nao foi possivel instalar o vsce.
        goto fim
    )
)

cd vscode

del /q *.vsix >nul 2>&1

echo Gerando extensao...

call vsce package --allow-missing-repository --skip-license

if errorlevel 1 (
    echo.
    echo ERRO: nao foi possivel gerar a extensao do VS Code.
    cd ..
    goto fim
)

for %%F in (*.vsix) do (
    echo Instalando extensao no VS Code...
    code --install-extension "%%F"
    echo.
    echo Suporte da Solua instalado no VS Code!
    goto instalado
)

echo.
echo ERRO: nao foi possivel encontrar o arquivo .vsix.

:instalado
cd ..

:fim

echo.
echo Solua instalado em %cd%.
echo Use com: python soll.py seu-arquivo.soll
pause

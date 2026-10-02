#!/bin/bash

echo "ONLY AVAILABLE IN PORTUGUESE!! -- APENAS DISPONÍVEL EM PORTUGUÊS!!"
sleep 1

if [ -d "Solua" ]; then
    echo "A pasta Solua já existe."
    cd Solua || exit 1
else
    git clone https://github.com/batata123muitoboa-dot/Solua.git || exit 1
    cd Solua || exit 1
fi

echo
echo "Solua instalado em $(pwd)."
echo

read -r -p "Fazer Visual Studio Code suportar Solua? [S/n] " RESPOSTA < /dev/tty

if [[ -z "$RESPOSTA" || "$RESPOSTA" =~ ^[Ss]$ ]]; then

    if ! command -v npm >/dev/null 2>&1; then
        echo
        echo "npm não está instalado."
        echo "Instale nodejs e npm"
    else
        echo
        echo "Instalando suporte da Solua no VS Code..."

        if ! command -v vsce >/dev/null 2>&1; then
            echo "vsce não encontrado."
            echo "Instalando @vscode/vsce..."
            sudo npm install -g @vscode/vsce || exit 1
        fi

        cd vscode || exit 1

        rm -f ./*.vsix

        echo "Gerando extensão..."

        if vsce package --allow-missing-repository --skip-license; then
            VSIX=$(find . -maxdepth 1 -name "*.vsix" -print -quit)

            if [ -n "$VSIX" ]; then
                echo "Instalando extensão no VS Code..."
                code --install-extension "$VSIX"

                echo
                echo "Suporte da Solua instalado no VS Code!"
            else
                echo
                echo "ERRO: não foi possível encontrar o arquivo .vsix."
            fi
        else
            echo
            echo "ERRO: não foi possível gerar a extensão do VS Code."
        fi

        cd ..
    fi

else
    echo
    echo "Suporte do VS Code ignorado."
fi

echo
echo "Solua instalado em $(pwd)."
echo "Use com: python soll.py seu-arquivo.soll"

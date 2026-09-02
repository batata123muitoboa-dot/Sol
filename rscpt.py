import sys
import re
import random
import ast
import time
import tkinter as tk

memoria = {}
funcoes = {}
elementos_ui = {}

def substituir_operadores(expressao):
    expressao = re.sub(r'\bdiferente\b', '!=', expressao)
    expressao = re.sub(r'\bmenos\+\b', '<=', expressao)
    expressao = re.sub(r'\bmais\+\b', '>=', expressao)
    expressao = re.sub(r'\bmenos\b', '<', expressao)
    expressao = re.sub(r'\bmais\b', '>', expressao)
    expressao = re.sub(r'\bigual\b', '==', expressao)
    expressao = re.sub(r'\be\b', ' and ', expressao)
    expressao = re.sub(r'\bou\b', ' or ', expressao)
    return expressao

def separar_argumentos(texto):
    argumentos = []
    atual = ""
    aspas = None
    nivel = 0

    for char in texto:
        if char in ('"', "'"):
            if aspas is None:
                aspas = char
            elif aspas == char:
                aspas = None

        elif char == "(" and aspas is None:
            nivel += 1

        elif char == ")" and aspas is None:
            nivel -= 1

        if char == "," and aspas is None and nivel == 0:
            argumentos.append(atual.strip())
            atual = ""
        else:
            atual += char

    if atual.strip():
        argumentos.append(atual.strip())

    return argumentos

def avaliar_expressao(expr):
    expr_limpa = str(expr).strip()
    
    if expr_limpa in memoria:
        return memoria[expr_limpa]

    if expr_limpa.startswith("aleatorio(") and expr_limpa.endswith(")"):
        args = expr_limpa[10:-1].split(",")
        p1 = int(avaliar_expressao(args[0]))
        p2 = int(avaliar_expressao(args[1]))
        return random.randint(p1, p2)

    if expr_limpa.startswith("item(") and expr_limpa.endswith(")"):
        args = expr_limpa[5:-1].split(",")
        nome_lista = args[0].strip()
        posicao = int(avaliar_expressao(args[1])) - 1
        lista_val = memoria.get(nome_lista, [])
        if isinstance(lista_val, list) and 0 <= posicao < len(lista_val):
            return lista_val[posicao]
        return ""

    if expr_limpa.startswith("tamanho(") and expr_limpa.endswith(")"):
        nome_lista = expr_limpa[8:-1].strip()
        lista_val = memoria.get(nome_lista, [])
        if isinstance(lista_val, list):
            return len(lista_val)
        return 0

    if expr_limpa.startswith("[") and expr_limpa.endswith("]"):
        conteudo = expr_limpa[1:-1]
        itens = [i.strip().strip('"').strip("'") for i in conteudo.split(",") if i.strip()]
        return itens

    if "+" in expr_limpa and ("'" in expr_limpa or '"' in expr_limpa or any(v in expr_limpa for v in memoria)):
        partes = expr_limpa.split("+")
        resultado_final = ""
        for p in partes:
            p_limpo = p.strip()
            if p_limpo in memoria:
                resultado_final += str(memoria[p_limpo])
            else:

                texto_puro = p_limpo.strip('"').strip("'")
                resultado_final += texto_puro
        return resultado_final

    expr_proc = substituir_operadores(expr_limpa)
    
    for var, val in sorted(memoria.items(), key=lambda x: len(x[0]), reverse=True):
        # MUDA ESTA LINHA: se for string, coloca entre aspas para o eval não se perder
        val_str = f'"{val}"' if isinstance(val, str) else str(val)
        expr_proc = re.sub(r'\b' + re.escape(var) + r'\b', val_str, expr_proc)
        
    try:
        return eval(expr_proc)
    except:
        return False

def executar_codigo(linhas):
    global elementos_ui
    i = 0
    num_linhas = len(linhas)

    while i < num_linhas:
        linha = linhas[i].strip()

        if not linha or linha.startswith("comentario") or linha.startswith("--") or linha.startswith("#"):
            i += 1
            continue

        if linha.startswith("funcao "):
            cabecalho = linha[7:].strip()
            nome_func = cabecalho[:cabecalho.index("(")].strip()
            params_str = cabecalho[cabecalho.index("(")+1:cabecalho.index(")")].strip()
            params = [p.strip() for p in params_str.split(",") if p.strip()]

            bloco_func = []
            i += 1
            aninhamento = 1
            while i < num_linhas and aninhamento > 0:
                l_sub = linhas[i].strip()
                if l_sub.startswith("funcao ") or l_sub.startswith("se ") or l_sub.startswith("enquanto ") or l_sub.startswith("para ") or l_sub.startswith("janela "):
                    aninhamento += 1
                elif l_sub == "fim":
                    aninhamento -= 1
                    if aninhamento == 0:
                        break
                bloco_func.append(linhas[i])
                i += 1

            funcoes[nome_func] = {"params": params, "corpo": bloco_func}

        elif "(" in linha and linha.endswith(")") and linha[:linha.index("(")].strip() in funcoes:
            nome_func = linha[:linha.index("(")].strip()
            args_str = linha[linha.index("(")+1:-1].strip()
            args = [avaliar_expressao(a) for a in args_str.split(",") if a.strip()]
            
            func_data = funcoes[nome_func]
            for p_nome, p_val in zip(func_data["params"], args):
                memoria[p_nome] = p_val
                
            executar_codigo(func_data["corpo"])

        elif linha.startswith("set "):
            conteudo = linha[4:].strip()
            var, val = conteudo.split(",", 1) if "," in conteudo else conteudo.split(" ", 1)
            var_nome = var.strip()
            val_str = val.strip()

            if val_str.startswith("pegar(") and val_str.endswith(")"):
                id_cmp = val_str[6:-1].strip().strip('"').strip("'")
                if id_cmp in elementos_ui:
                    widget = elementos_ui[id_cmp]
                    if isinstance(widget, tk.Entry):
                        val_obtido = widget.get()
                        try:
                            memoria[var_nome] = float(val_obtido) if "." in val_obtido else int(val_obtido)
                        except ValueError:
                            memoria[var_nome] = val_obtido
                    else:
                        memoria[var_nome] = ""
                else:
                    memoria[var_nome] = ""
            else:
                memoria[var_nome] = avaliar_expressao(val_str)

        elif linha.startswith("adicionar(") and linha.endswith(")"):
            args = linha[10:-1].split(",")
            nome_lista = args[0].strip()
            valor_novo = avaliar_expressao(args[1].strip())
            
            if nome_lista in memoria and isinstance(memoria[nome_lista], list):
                memoria[nome_lista].append(valor_novo)

        elif linha.startswith("atualizar_texto(") and linha.endswith(")"):
            args = [avaliar_expressao(a) for a in separar_argumentos(linha[16:-1])]
            if len(args) >= 2:
                id_alvo = str(args[0])
                novo_texto = str(args[1])
                if id_alvo in elementos_ui:
                    widget = elementos_ui[id_alvo]
                    if isinstance(widget, tk.Label):
                        widget.config(text=novo_texto)

        elif linha.startswith("escrever(") and linha.endswith(")"):
            conteudo = linha[9:-1]
            args = [
                avaliar_expressao(arg.strip())
                for arg in separar_argumentos(conteudo)
                if arg.strip()
            ]
            print(*args)

        elif linha.startswith("ler(") and linha.endswith(")"):
            conteudo_ler = linha[4:-1]
            if "," in conteudo_ler:
                var_nome, msg_expr = conteudo_ler.split(",", 1)
                var_nome = var_nome.strip()
                mensagem = avaliar_expressao(msg_expr.strip())
                entrada = input(mensagem)
            else:
                var_nome = conteudo_ler.strip()
                entrada = input()

            try:
                memoria[var_nome] = float(entrada) if "." in entrada else int(entrada)
            except ValueError:
                memoria[var_nome] = entrada

        elif linha.startswith("esperar(") and linha.endswith(")"):
            tempo_str = linha[8:-1].strip()
            tempo_val = avaliar_expressao(tempo_str)
            try:
                time.sleep(float(tempo_val))
            except (ValueError, TypeError):
                print(f"Erro no .rscpt: tempo inválido em '{linha}'")

        elif linha.startswith("janela("):
            elementos_ui = {}

            tem_bloco = "entao" in linha
            cabecalho_janela = linha[7:linha.index("entao")].strip() if tem_bloco else linha[7:-1].strip()
            if cabecalho_janela.endswith(")"):
                cabecalho_janela = cabecalho_janela[:-1].strip()

            args_janela = [avaliar_expressao(a.strip()) for a in cabecalho_janela.split(",")]
            titulo = str(args_janela[0]) if len(args_janela) > 0 else "Janela .rscpt"
            largura = int(args_janela[1]) if len(args_janela) > 1 else 400
            altura = int(args_janela[2]) if len(args_janela) > 2 else 300
            bg_janela = str(args_janela[3]).strip() if len(args_janela) > 3 else None

            bloco_janela = []
            if tem_bloco:
                i += 1
                aninhamento = 1
                while i < num_linhas and aninhamento > 0:
                    l_sub = linhas[i].strip()
                    if l_sub.startswith("janela ") or l_sub.startswith("se ") or l_sub.startswith("enquanto ") or l_sub.startswith("para ") or l_sub.startswith("funcao "):
                        aninhamento += 1
                    elif l_sub == "fim":
                        aninhamento -= 1
                        if aninhamento == 0:
                            break
                    bloco_janela.append(linhas[i])
                    i += 1

            root = tk.Tk()
            root.title(titulo)
            root.geometry(f"{largura}x{altura}")
            if bg_janela:
                root.config(bg=bg_janela)
            
            if not bloco_janela:
                label = tk.Label(root, text=f"Janela gerada pelo .rscpt!\nTítulo: {titulo}", font=("Arial", 12))
                label.pack(expand=True)
            else:
                for l_ui in bloco_janela:
                    l_ui = l_ui.strip()
                    if not l_ui or l_ui.startswith("--") or l_ui.startswith("#"):
                        continue

                    if l_ui.startswith("texto(") and l_ui.endswith(")"):
                        args_txt = [avaliar_expressao(a.strip()) for a in l_ui[6:-1].split(",")]
                        
                        id_lbl, t_val, t_fg, t_bg = None, "", None, None
                        
                        if len(args_txt) >= 4:
                            id_lbl, t_val, t_fg, t_bg = str(args_txt[0]), str(args_txt[1]), str(args_txt[2]), str(args_txt[3])
                        elif len(args_txt) == 3:
                            if str(args_txt[0]).startswith("id_") or len(str(args_txt[0])) < 15 and not any(c in str(args_txt[0]) for c in " .!?"):
                                id_lbl, t_val, t_fg = str(args_txt[0]), str(args_txt[1]), str(args_txt[2])
                            else:
                                t_val, t_fg, t_bg = str(args_txt[0]), str(args_txt[1]), str(args_txt[2])
                        elif len(args_txt) == 2:
                            if str(args_txt[0]).startswith("id_") or len(str(args_txt[0])) < 15 and not any(c in str(args_txt[0]) for c in " .!?"):
                                id_lbl, t_val = str(args_txt[0]), str(args_txt[1])
                            else:
                                t_val, t_fg = str(args_txt[0]), str(args_txt[1])
                        else:
                            t_val = str(args_txt[0]) if len(args_txt) > 0 else ""

                        kwargs = {"font": ("Arial", 11)}
                        if t_fg: kwargs["fg"] = t_fg
                        if t_bg: kwargs["bg"] = t_bg
                        elif bg_janela: kwargs["bg"] = bg_janela

                        lbl = tk.Label(root, text=t_val, **kwargs)
                        lbl.pack(pady=5)
                        if id_lbl:
                            elementos_ui[id_lbl] = lbl

                    elif l_ui.startswith("caixa_texto(") and l_ui.endswith(")"):
                        args_cx = [avaliar_expressao(a.strip()) for a in l_ui[12:-1].split(",")]
                        id_cx = str(args_cx[0]) if len(args_cx) > 0 else "cx"
                        cx_bg = str(args_cx[1]).strip() if len(args_cx) > 1 else None
                        cx_fg = str(args_cx[2]).strip() if len(args_cx) > 2 else None

                        kwargs = {"font": ("Arial", 11), "width": 25}
                        if cx_bg: kwargs["bg"] = cx_bg
                        if cx_fg: kwargs["fg"] = cx_fg

                        entry = tk.Entry(root, **kwargs)
                        entry.pack(pady=5)
                        elementos_ui[id_cx] = entry

                    elif l_ui.startswith("botao(") and l_ui.endswith(")"):
                        args_btn = [avaliar_expressao(a.strip()) for a in l_ui[6:-1].split(",")]
                        
                        b_texto = str(args_btn[0]) if len(args_btn) > 0 else "Botão"
                        b_acao = str(args_btn[1]).strip() if len(args_btn) > 1 else ""
                        b_bg = str(args_btn[2]).strip() if len(args_btn) > 2 else None
                        b_fg = str(args_btn[3]).strip() if len(args_btn) > 3 else None

                        def criar_comando(acao_nome):
                            def acao_clique():
                                if acao_nome in funcoes:
                                    executar_codigo(funcoes[acao_nome]["corpo"])
                                elif acao_nome == "sair":
                                    root.destroy()
                            return acao_clique

                        kwargs = {"font": ("Arial", 10, "bold"), "padx": 12, "pady": 5}
                        if b_bg: kwargs["bg"] = b_bg
                        if b_fg: kwargs["fg"] = b_fg
                        if b_acao: kwargs["command"] = criar_comando(b_acao)

                        tk.Button(root, text=b_texto, **kwargs).pack(pady=5)

            root.mainloop()

        elif linha.startswith("se ") and "entao" in linha:
            condicao_str = linha[3:linha.index("entao")].strip()

            if " == " in condicao_str and not condicao_str.replace("==", "").strip().replace(".", "").isdigit():
                partes_cond = condicao_str.split("==")
                p1 = str(avaliar_expressao(partes_cond[0].strip()))
                p2 = str(avaliar_expressao(partes_cond[1].strip()))
                resultado_condicao = (p1 == p2)
            else:
                resultado_condicao = bool(avaliar_expressao(condicao_str))

            bloco_se, bloco_senao = [], []
            em_senao = False
            i += 1
            aninhamento = 1
            
            while i < num_linhas and aninhamento > 0:
                l_sub = linhas[i].strip()
                if l_sub.startswith("se ") or l_sub.startswith("enquanto ") or l_sub.startswith("funcao ") or l_sub.startswith("para ") or l_sub.startswith("janela "):
                    aninhamento += 1
                elif l_sub == "fim":
                    aninhamento -= 1
                    if aninhamento == 0:
                        break
                elif l_sub == "senao" and aninhamento == 1:
                    em_senao = True
                    i += 1
                    continue

                if em_senao:
                    bloco_senao.append(linhas[i])
                else:
                    bloco_se.append(linhas[i])
                i += 1

            if resultado_condicao:
                executar_codigo(bloco_se)
            else:
                executar_codigo(bloco_senao)

        elif linha.startswith("enquanto ") and "entao" in linha:
            condicao_str = linha[9:linha.index("entao")].strip()
            bloco_enquanto = []
            i += 1
            aninhamento = 1
            
            while i < num_linhas and aninhamento > 0:
                l_sub = linhas[i].strip()
                if l_sub.startswith("enquanto ") or l_sub.startswith("se ") or l_sub.startswith("funcao ") or l_sub.startswith("para ") or l_sub.startswith("janela "):
                    aninhamento += 1
                elif l_sub == "fim":
                    aninhamento -= 1
                    if aninhamento == 0:
                        break
                bloco_enquanto.append(linhas[i])
                i += 1

            while bool(avaliar_expressao(condicao_str)):
                executar_codigo(bloco_enquanto)

        elif linha.startswith("para ") and "entao" in linha:
            cabecalho_para = linha[5:linha.index("entao")].strip()
            bloco_para = []
            i += 1
            aninhamento = 1
            
            while i < num_linhas and aninhamento > 0:
                l_sub = linhas[i].strip()
                if l_sub.startswith("para ") or l_sub.startswith("se ") or l_sub.startswith("enquanto ") or l_sub.startswith("funcao ") or l_sub.startswith("janela "):
                    aninhamento += 1
                elif l_sub == "fim":
                    aninhamento -= 1
                    if aninhamento == 0:
                        break
                bloco_para.append(linhas[i])
                i += 1

            if " em " in cabecalho_para:
                var_nome, lista_expr = cabecalho_para.split(" em ")
                var_nome = var_nome.strip()
                lista_val = avaliar_expressao(lista_expr.strip())
                
                if isinstance(lista_val, list):
                    for elemento in lista_val:
                        memoria[var_nome] = elemento
                        executar_codigo(bloco_para)

            elif " de " in cabecalho_para and " até " in cabecalho_para:
                partes = cabecalho_para.split(" de ")
                var_nome = partes[0].strip()
                limites = partes[1].split(" até ")
                inicio = int(avaliar_expressao(limites[0].strip()))
                fim_val = int(avaliar_expressao(limites[1].strip()))
                
                passo = 1 if inicio <= fim_val else -1
                for val_atual in range(inicio, fim_val + passo, passo):
                    memoria[var_nome] = val_atual
                    executar_codigo(bloco_para)

        elif linha == "fim" or linha == "senao":
            pass

        else:
            print(f"Erro no .rscpt na linha {i+1}: '{linha}'")

        i += 1

def main():
    if len(sys.argv) < 2 or not sys.argv[1].endswith(".rscpt"):
        print("Uso: python rscpt.py <arquivo.rscpt>")
        return

    try:
        with open(sys.argv[1], "r", encoding="utf-8") as f:
            executar_codigo(f.readlines())
    except FileNotFoundError:
        print(f"Erro: Arquivo '{sys.argv[1]}' não encontrado.")

if __name__ == "__main__":
    main()

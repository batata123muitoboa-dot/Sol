import sys
import re
import random
import ast
import time
import os
import tkinter as tk

memoria = {}
funcoes = {}
elementos_ui = {}

class InterrupcaoBreak(Exception):
    """Exceção interna para interromper loops (pare/quebrar)."""
    pass

class RetornoFuncao(Exception):
    """Exceção interna para capturar o retorno de funções (retornar)."""
    def __init__(self, valor):
        self.valor = valor

def substituir_operadores(expressao):
    expressao = re.sub(r'\bdiferente\b', '!=', expressao)
    expressao = re.sub(r'\bmenos\+\b', '<=', expressao)
    expressao = re.sub(r'\bmais\+\b', '>=', expressao)
    expressao = re.sub(r'\bmenos\b', '<', expressao)
    expressao = re.sub(r'\bmais\b', '>', expressao)
    expressao = re.sub(r'\bigual\b', '==', expressao)
    expressao = re.sub(r'\be\b', ' and ', expressao)
    expressao = re.sub(r'\bou\b', ' or ', expressao)
    expressao = re.sub(r'\bnao\b', ' not ', expressao)
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

        elif char in ("(", "[", "{") and aspas is None:
            nivel += 1

        elif char in (")", "]", "}") and aspas is None:
            nivel -= 1

        if char == "," and aspas is None and nivel == 0:
            argumentos.append(atual.strip())
            atual = ""
        else:
            atual += char

    if atual.strip():
        argumentos.append(atual.strip())

    return argumentos

def buscar_var(nome_var, escopo_local=None):
    """Busca primeiro no escopo local da função e depois na memória global."""
    if escopo_local is not None and nome_var in escopo_local:
        return escopo_local[nome_var]
    return memoria.get(nome_var, None)

def avaliar_expressao(expr, escopo_local=None):
    expr_limpa = str(expr).strip()
    
    # Suporte a leitura de dicionários via ponto (ex: jogador.vida)
    if "." in expr_limpa and not expr_limpa.replace(".", "").isdigit() and not (expr_limpa.startswith('"') or expr_limpa.startswith("'")):
        if not any(op in expr_limpa for op in ["+", "-", "*", "/", ">", "<", "=="]):
            partes = expr_limpa.split(".", 1)
            obj = buscar_var(partes[0].strip(), escopo_local)
            if isinstance(obj, dict):
                return obj.get(partes[1].strip(), "")

    val_var = buscar_var(expr_limpa, escopo_local)
    if val_var is not None:
        return val_var

    # Criacao de dicionarios { chave: valor, chave2: valor2 }
    if expr_limpa.startswith("{") and expr_limpa.endswith("}"):
        conteudo = expr_limpa[1:-1].strip()
        dic = {}
        if conteudo:
            pares = separar_argumentos(conteudo)
            for p in pares:
                if ":" in p:
                    k, v = p.split(":", 1)
                    chave = k.strip().strip('"\'')
                    dic[chave] = avaliar_expressao(v.strip(), escopo_local)
        return dic

    # Manipulação de texto sem deixar aspas vazando
    if expr_limpa.startswith("maiusculo(") and expr_limpa.endswith(")"):
        val = avaliar_expressao(expr_limpa[10:-1], escopo_local)
        return str(val).strip('"\'').upper()

    if expr_limpa.startswith("minusculo(") and expr_limpa.endswith(")"):
        val = avaliar_expressao(expr_limpa[10:-1], escopo_local)
        return str(val).strip('"\'').lower()

    if expr_limpa.startswith("tamanho(") and expr_limpa.endswith(")"):
        arg = expr_limpa[8:-1].strip()
        val = buscar_var(arg, escopo_local)
        if val is None:
            val = avaliar_expressao(arg, escopo_local)
        if isinstance(val, (list, str, dict)):
            return len(val)
        return 0

    if expr_limpa.startswith("aleatorio(") and expr_limpa.endswith(")"):
        args = expr_limpa[10:-1].split(",")
        p1 = int(avaliar_expressao(args[0], escopo_local))
        p2 = int(avaliar_expressao(args[1], escopo_local))
        return random.randint(p1, p2)

    # Acesso a listas e dicionarios via item(colecao, chave_ou_posicao)
    if expr_limpa.startswith("item(") and expr_limpa.endswith(")"):
        args = separar_argumentos(expr_limpa[5:-1])
        nome_col = args[0].strip()
        chave_pos = avaliar_expressao(args[1], escopo_local)
        
        colecao = buscar_var(nome_col, escopo_local)
        if colecao is None:
            colecao = avaliar_expressao(nome_col, escopo_local)

        if isinstance(colecao, list):
            posicao = int(chave_pos) - 1
            if 0 <= posicao < len(colecao):
                return colecao[posicao]
        elif isinstance(colecao, dict):
            return colecao.get(str(chave_pos), "")
        return ""

    # Chamada de função
    if "(" in expr_limpa and expr_limpa.endswith(")"):
        nome_f = expr_limpa[:expr_limpa.index("(")].strip()
        if nome_f in funcoes:
            args_str = expr_limpa[expr_limpa.index("(")+1:-1].strip()
            args = [avaliar_expressao(a, escopo_local) for a in separar_argumentos(args_str) if a.strip()]
            
            func_data = funcoes[nome_f]
            novo_escopo_local = {}
            for p_nome, p_val in zip(func_data["params"], args):
                novo_escopo_local[p_nome] = p_val
                
            try:
                executar_codigo(func_data["corpo"], novo_escopo_local)
            except RetornoFuncao as ret:
                return ret.valor
            return None

    if expr_limpa.startswith("[") and expr_limpa.endswith("]"):
        conteudo = expr_limpa[1:-1]
        itens = [avaliar_expressao(i.strip(), escopo_local) for i in separar_argumentos(conteudo) if i.strip()]
        return itens

    # Se for soma de texto explícito (entre aspas)
    if "+" in expr_limpa and ("'" in expr_limpa or '"' in expr_limpa):
        partes = expr_limpa.split("+")
        resultado_final = ""
        for p in partes:
            p_limpo = p.strip()
            val_p = buscar_var(p_limpo, escopo_local)
            if val_p is not None:
                resultado_final += str(val_p)
            else:
                resultado_final += p_limpo.strip('"').strip("'")
        return resultado_final

    # Processamento de variáveis e operadores matemáticos/lógicos
    expr_proc = substituir_operadores(expr_limpa)
    
    contexto = {}
    contexto.update(memoria)
    if escopo_local:
        contexto.update(escopo_local)

    # TRATAMENTO DE PONTO EM PROPRIEDADES (ex: jogador.vida -> 100)
    for var, val in contexto.items():
        if isinstance(val, dict):
            for k, v in val.items():
                prop = f"{var}.{k}"
                v_str = f'"{v}"' if isinstance(v, str) else str(v)
                expr_proc = re.sub(r'\b' + re.escape(prop) + r'\b', v_str, expr_proc)

    for var, val in sorted(contexto.items(), key=lambda x: len(x[0]), reverse=True):
        if not isinstance(val, (dict, list)):
            val_str = f'"{val}"' if isinstance(val, str) else str(val)
            expr_proc = re.sub(r'\b' + re.escape(var) + r'\b', val_str, expr_proc)
        
    try:
        res = eval(expr_proc)
        if isinstance(res, float) and res.is_integer():
            return int(res)
        return res
    except:
        return expr_limpa.strip('"\'')

def remover_comentario(linha):
    """Remove comentários # ou -- somente se NÃO estiverem entre aspas."""
    dentro_aspas = False
    char_aspas = None
    i = 0
    while i < len(linha):
        c = linha[i]
        if c in ('"', "'"):
            if not dentro_aspas:
                dentro_aspas = True
                char_aspas = c
            elif char_aspas == c:
                dentro_aspas = False
                char_aspas = None
        elif not dentro_aspas:
            if c == '#' or (c == '-' and i + 1 < len(linha) and linha[i+1] == '-'):
                return linha[:i].strip()
        i += 1
    return linha.strip()

def executar_codigo(linhas, escopo_local=None):
    global elementos_ui
    i = 0
    num_linhas = len(linhas)

    while i < num_linhas:
        linha = remover_comentario(linhas[i])

        if not linha or linha.startswith("comentario"):
            i += 1
            continue

        if linha == "limpar()" or linha == "limpar":
            os.system('cls' if os.name == 'nt' else 'clear')

        elif linha in ("pare", "quebrar"):
            raise InterrupcaoBreak()

        elif linha.startswith("retornar ") or linha.startswith("retornar(") or linha == "retornar":
            if "(" in linha and linha.endswith(")"):
                val_expr = linha[9:-1].strip()
            elif len(linha) > 8:
                val_expr = linha[8:].strip()
            else:
                val_expr = ""
            
            valor_retorno = avaliar_expressao(val_expr, escopo_local) if val_expr else None
            raise RetornoFuncao(valor_retorno)

        elif linha.startswith("funcao "):
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
            args = [avaliar_expressao(a, escopo_local) for a in separar_argumentos(args_str) if a.strip()]
            
            func_data = funcoes[nome_func]
            novo_escopo_local = {}
            for p_nome, p_val in zip(func_data["params"], args):
                novo_escopo_local[p_nome] = p_val
                
            try:
                executar_codigo(func_data["corpo"], novo_escopo_local)
            except RetornoFuncao:
                pass

        # Atribuição Local (setl) ou Global (set)
        elif linha.startswith("set ") or linha.startswith("setl "):
            e_local = linha.startswith("setl ")
            conteudo = linha[5:].strip() if e_local else linha[4:].strip()
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
                            val_final = float(val_obtido) if "." in val_obtido else int(val_obtido)
                        except ValueError:
                            val_final = val_obtido
                    else:
                        val_final = ""
                else:
                    val_final = ""
            else:
                val_final = avaliar_expressao(val_str, escopo_local)

            # Alteração de campo de dicionário ex: set jogador.vida, 80
            if "." in var_nome and not (var_nome.startswith('"') or var_nome.startswith("'")):
                partes = var_nome.split(".", 1)
                obj = buscar_var(partes[0].strip(), escopo_local)
                if isinstance(obj, dict):
                    obj[partes[1].strip()] = val_final
            else:
                if e_local and escopo_local is not None:
                    escopo_local[var_nome] = val_final
                else:
                    memoria[var_nome] = val_final

        elif linha.startswith("adicionar(") and linha.endswith(")"):
            args = separar_argumentos(linha[10:-1])
            nome_lista = args[0].strip()
            valor_novo = avaliar_expressao(args[1].strip(), escopo_local)
            
            alvo = buscar_var(nome_lista, escopo_local)
            if isinstance(alvo, list):
                alvo.append(valor_novo)

        elif linha.startswith("atualizar_texto(") and linha.endswith(")"):
            args = [avaliar_expressao(a, escopo_local) for a in separar_argumentos(linha[16:-1])]
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
                avaliar_expressao(arg.strip(), escopo_local)
                for arg in separar_argumentos(conteudo)
                if arg.strip()
            ]
            print(*args)

        elif linha.startswith("ler(") and linha.endswith(")"):
            conteudo_ler = linha[4:-1]
            if "," in conteudo_ler:
                var_nome, msg_expr = conteudo_ler.split(",", 1)
                var_nome = var_nome.strip()
                mensagem = avaliar_expressao(msg_expr.strip(), escopo_local)
                entrada = input(mensagem)
            else:
                var_nome = conteudo_ler.strip()
                entrada = input()

            try:
                val_lido = float(entrada) if "." in entrada else int(entrada)
            except ValueError:
                val_lido = entrada

            if escopo_local is not None and var_nome in escopo_local:
                escopo_local[var_nome] = val_lido
            else:
                memoria[var_nome] = val_lido

        elif linha.startswith("esperar(") and linha.endswith(")"):
            tempo_str = linha[8:-1].strip()
            tempo_val = avaliar_expressao(tempo_str, escopo_local)
            try:
                time.sleep(float(tempo_val))
            except (ValueError, TypeError):
                print(f"Erro no .rscpl: tempo inválido em '{linha}'")

        elif linha.startswith("janela("):
            elementos_ui = {}

            tem_bloco = "entao" in linha
            cabecalho_janela = linha[7:linha.index("entao")].strip() if tem_bloco else linha[7:-1].strip()
            if cabecalho_janela.endswith(")"):
                cabecalho_janela = cabecalho_janela[:-1].strip()

            args_janela = [avaliar_expressao(a.strip(), escopo_local) for a in separar_argumentos(cabecalho_janela)]
            titulo = str(args_janela[0]) if len(args_janela) > 0 else "Janela .rscpl"
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
                label = tk.Label(root, text=f"Janela gerada pelo .rscpl!\nTítulo: {titulo}", font=("Arial", 12))
                label.pack(expand=True)
            else:
                for l_ui in bloco_janela:
                    l_ui = l_ui.strip()
                    if not l_ui or l_ui.startswith("--") or l_ui.startswith("#"):
                        continue

                    if l_ui.startswith("texto(") and l_ui.endswith(")"):
                        args_txt = [avaliar_expressao(a.strip(), escopo_local) for a in separar_argumentos(l_ui[6:-1])]
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
                        args_cx = [avaliar_expressao(a.strip(), escopo_local) for a in separar_argumentos(l_ui[12:-1])]
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
                        args_btn = [avaliar_expressao(a.strip(), escopo_local) for a in separar_argumentos(l_ui[6:-1])]
                        b_texto = str(args_btn[0]) if len(args_btn) > 0 else "Botão"
                        b_acao = str(args_btn[1]).strip() if len(args_btn) > 1 else ""
                        b_bg = str(args_btn[2]).strip() if len(args_btn) > 2 else None
                        b_fg = str(args_btn[3]).strip() if len(args_btn) > 3 else None

                        def criar_comando(acao_nome):
                            def acao_clique():
                                if acao_nome in funcoes:
                                    try:
                                        executar_codigo(funcoes[acao_nome]["corpo"], escopo_local)
                                    except RetornoFuncao:
                                        pass
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
                p1 = str(avaliar_expressao(partes_cond[0].strip(), escopo_local))
                p2 = str(avaliar_expressao(partes_cond[1].strip(), escopo_local))
                resultado_condicao = (p1 == p2)
            else:
                resultado_condicao = bool(avaliar_expressao(condicao_str, escopo_local))

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
                executar_codigo(bloco_se, escopo_local)
            else:
                executar_codigo(bloco_senao, escopo_local)

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

            while bool(avaliar_expressao(condicao_str, escopo_local)):
                try:
                    executar_codigo(bloco_enquanto, escopo_local)
                except InterrupcaoBreak:
                    break

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

            try:
                if " em " in cabecalho_para:
                    var_nome, lista_expr = cabecalho_para.split(" em ")
                    var_nome = var_nome.strip()
                    lista_val = avaliar_expressao(lista_expr.strip(), escopo_local)
                    
                    if isinstance(lista_val, (list, dict)):
                        for elemento in lista_val:
                            if escopo_local is not None:
                                escopo_local[var_nome] = elemento
                            else:
                                memoria[var_nome] = elemento
                            try:
                                executar_codigo(bloco_para, escopo_local)
                            except InterrupcaoBreak:
                                break

                elif " de " in cabecalho_para and " até " in cabecalho_para:
                    partes = cabecalho_para.split(" de ")
                    var_nome = partes[0].strip()
                    limites = partes[1].split(" até ")
                    inicio = int(avaliar_expressao(limites[0].strip(), escopo_local))
                    fim_val = int(avaliar_expressao(limites[1].strip(), escopo_local))
                    
                    passo = 1 if inicio <= fim_val else -1
                    for val_atual in range(inicio, fim_val + passo, passo):
                        if escopo_local is not None:
                            escopo_local[var_nome] = val_atual
                        else:
                            memoria[var_nome] = val_atual
                        try:
                            executar_codigo(bloco_para, escopo_local)
                        except InterrupcaoBreak:
                            break
            except InterrupcaoBreak:
                pass

        elif linha == "fim" or linha == "senao":
            pass

        else:
            print(f"Erro no .rscpl na linha {i+1}: '{linha}'")

        i += 1

def main():
    if len(sys.argv) < 2 or not sys.argv[1].endswith(".rscpl"):
        print("Uso: python rscpl.py <arquivo.rscpl>")
        return

    try:
        with open(sys.argv[1], "r", encoding="utf-8") as f:
            executar_codigo(f.readlines())
    except FileNotFoundError:
        print(f"Erro: Arquivo '{sys.argv[1]}' não encontrado.")

if __name__ == "__main__":
    main()

#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
/* Solua interpreter - C/GTK3 port */
typedef enum  {
    V_NULL, V_NUM, V_STR, V_BOOL, V_LIST, V_DICT
}
ValueType;
typedef struct Value Value;
typedef struct Function Function;
typedef struct Interpreter Interpreter;
typedef struct Scope Scope;
struct Value  {
    ValueType type;
    double num;
    gboolean boolean;
    char *str;
    GPtrArray *list;
    GHashTable *dict;
}
;
struct Function  {
    GPtrArray *params;
    GPtrArray *body;
}
;
struct Scope  {
    GHashTable *vars;
    Scope *parent;
}
;
typedef enum  {
    FLOW_OK, FLOW_BREAK, FLOW_RETURN
}
Flow;
typedef struct  {
    Flow flow;
    Value *value;
}
ExecResult;
struct Interpreter  {
    GHashTable *globals;
    GHashTable *functions;
    GHashTable *ui;
    Scope *scope;
    gboolean gtk_started;
    GtkWidget *window;
}
;
static Value *val_null(void);
static Value *val_num(double n);
static Value *val_bool(gboolean b);
static Value *val_str(const char *s);
static Value *val_list(void);
static Value *val_dict(void);
static Value *val_copy(const Value *v);
static void val_free(Value *v);
static char *trim(char *s);
static gboolean starts(const char *s,const char *p);
static Value *val_null(void) {
    Value*v=g_new0(Value,1);
    v->type=V_NULL;
    return v;
}
static Value *val_num(double n) {
    Value*v=g_new0(Value,1);
    v->type=V_NUM;
    v->num=n;
    return v;
}
static Value *val_bool(gboolean b) {
    Value*v=g_new0(Value,1);
    v->type=V_BOOL;
    v->boolean=b;
    return v;
}
static Value *val_str(const char*s) {
    Value*v=g_new0(Value,1);
    v->type=V_STR;
    v->str=g_strdup(s?s:"");
    return v;
}
static Value *val_list(void) {
    Value*v=g_new0(Value,1);
    v->type=V_LIST;
    v->list=g_ptr_array_new_with_free_func((GDestroyNotify)val_free);
    return v;
}
static Value *val_dict(void) {
    Value*v=g_new0(Value,1);
    v->type=V_DICT;
    v->dict=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)val_free);
    return v;
}
static Value *val_copy(const Value*v) {
    if(!v)return val_null();
    switch(v->type) {
        case V_NULL:return val_null();
        case V_NUM:return val_num(v->num);
        case V_BOOL:return val_bool(v->boolean);
        case V_STR:return val_str(v->str);
        case V_LIST: {
            Value*n=val_list();
            for(guint i=0;i<v->list->len;i++)g_ptr_array_add(n->list,val_copy(g_ptr_array_index(v->list,i)));
            return n;
        }
        case V_DICT: {
            Value*n=val_dict();
            GHashTableIter it;
            gpointer k,x;
            g_hash_table_iter_init(&it,v->dict);
            while(g_hash_table_iter_next(&it,&k,&x))g_hash_table_insert(n->dict,g_strdup(k),val_copy(x));
            return n;
        }
    }
    return val_null();
}
static void val_free(Value*v) {
    if(!v)return;
    if(v->type==V_STR)g_free(v->str);
    else if(v->type==V_LIST)g_ptr_array_free(v->list,TRUE);
    else if(v->type==V_DICT)g_hash_table_destroy(v->dict);
    g_free(v);
}
static char *trim(char*s) {
    while(g_ascii_isspace(*s))s++;
    char*e=s+strlen(s);
    while(e>s&&g_ascii_isspace(e[-1]))*--e=0;
    return s;
}
static gboolean starts(const char*s,const char*p) {
    return g_str_has_prefix(s,p);
}
static char *value_string(const Value*v) {
    if(!v)return g_strdup("");
    char buf[64];
    switch(v->type) {
        case V_NULL:return g_strdup("nulo");
        case V_BOOL:return g_strdup(v->boolean?"Verdadeiro":"Falso");
        case V_NUM:g_snprintf(buf,sizeof buf,"%.15g",v->num);
        return g_strdup(buf);
        case V_STR:return g_strdup(v->str);
        case V_LIST: {
            GString*s=g_string_new("[");
            for(guint i=0;i<v->list->len;i++) {
                if(i)g_string_append(s,", ");
                char*x=value_string(g_ptr_array_index(v->list,i));
                g_string_append(s,x);
                g_free(x);
            }
            g_string_append_c(s,']');
            return g_string_free(s,FALSE);
        }
        case V_DICT: {
            GString*s=g_string_new("{");
            GHashTableIter it;
            gpointer k,x;
            gboolean first=TRUE;
            g_hash_table_iter_init(&it,v->dict);
            while(g_hash_table_iter_next(&it,&k,&x)) {
                if(!first)g_string_append(s,", ");
                first=FALSE;
                char*z=value_string(x);
                g_string_append_printf(s,"%s: %s",(char*)k,z);
                g_free(z);
            }
            g_string_append_c(s,'}');
            return g_string_free(s,FALSE);
        }
    }
    return g_strdup("");
}
static gboolean truthy(const Value*v) {
    if(!v||v->type==V_NULL)return FALSE;
    if(v->type==V_BOOL)return v->boolean;
    if(v->type==V_NUM)return v->num!=0;
    if(v->type==V_STR)return v->str&&*v->str;
    if(v->type==V_LIST)return v->list->len>0;
    if(v->type==V_DICT)return g_hash_table_size(v->dict)>0;
    return FALSE;
}
static double number(const Value*v) {
    if(!v)return 0;
    if(v->type==V_NUM)return v->num;
    if(v->type==V_BOOL)return v->boolean;
    char*e=NULL;
    double d=(v->type==V_STR)?g_ascii_strtod(v->str,&e):0;
    return e&&*e==0?d:0;
}
static gboolean equalv(const Value*a,const Value*b) {
    if(a->type==V_NUM&&b->type==V_NUM)return a->num==b->num;
    if(a->type==V_BOOL&&b->type==V_BOOL)return a->boolean==b->boolean;
    if(a->type==V_STR&&b->type==V_STR)return strcmp(a->str,b->str)==0;
    if(a->type==V_NULL&&b->type==V_NULL)return TRUE;
    return strcmp(value_string(a),value_string(b))==0;
}
static Scope *scope_new(Scope*p) {
    Scope*s=g_new0(Scope,1);
    s->vars=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)val_free);
    s->parent=p;
    return s;
}
static void scope_free(Scope*s) {
    if(!s)return;
    g_hash_table_destroy(s->vars);
    g_free(s);
}
static Value *lookup(Interpreter*I,const char*n) {
    for(Scope*s=I->scope;s;s=s->parent) {
        Value*v=g_hash_table_lookup(s->vars,n);
        if(v)return v;
    }
    return g_hash_table_lookup(I->globals,n);
}
static void set_var(Interpreter*I,const char*n,Value*v,gboolean local) {
    if(local) {
        g_hash_table_replace(I->scope->vars,g_strdup(n),v);
        return;
    }
    for(Scope*s=I->scope;s;s=s->parent)if(g_hash_table_contains(s->vars,n)) {
        g_hash_table_replace(s->vars,g_strdup(n),v);
        return;
    }
    g_hash_table_replace(I->globals,g_strdup(n),v);
}
/* Expression parser */
typedef struct  {
    const char*s;
    size_t p;
    Interpreter*I;
}
Parser;
static void ws(Parser*p) {
    while(g_ascii_isspace(p->s[p->p]))p->p++;
}
static gboolean eat(Parser*p,const char*x) {
    ws(p);
    size_t n=strlen(x);
    if(g_str_has_prefix(p->s+p->p,x)) {
        p->p+=n;
        return TRUE;
    }
    return FALSE;
}
static char *ident(Parser*p) {
    ws(p);
    size_t a=p->p;
    if(!(g_ascii_isalpha(p->s[p->p])||p->s[p->p]=='_'))return NULL;
    p->p++;
    while(g_ascii_isalnum(p->s[p->p])||p->s[p->p]=='_')p->p++;
    return g_strndup(p->s+a,p->p-a);
}
static Value *expr_or(Parser*p);
static ExecResult execute_lines(Interpreter*,GPtrArray*);
static Value *call_builtin(Interpreter*I,const char*n,GPtrArray*args) {
    if(!strcmp(n,"maiusculo")) {
        char*s=args->len?value_string(g_ptr_array_index(args,0)):g_strdup("");
        for(char*c=s;*c;c++)*c=g_ascii_toupper(*c);
        Value*v=val_str(s);
        g_free(s);
        return v;
    }
    if(!strcmp(n,"minusculo")) {
        char*s=args->len?value_string(g_ptr_array_index(args,0)):g_strdup("");
        for(char*c=s;*c;c++)*c=g_ascii_tolower(*c);
        Value*v=val_str(s);
        g_free(s);
        return v;
    }
    if(!strcmp(n,"tamanho")) {
        Value*a=args->len?g_ptr_array_index(args,0):NULL;
        if(a) {
            if(a->type==V_STR)return val_num(strlen(a->str));
            if(a->type==V_LIST)return val_num(a->list->len);
            if(a->type==V_DICT)return val_num(g_hash_table_size(a->dict));
        }
        return val_num(0);
    }
    if(!strcmp(n,"aleatorio")) {
        int a=args->len?number(g_ptr_array_index(args,0)):0,b=args->len>1?number(g_ptr_array_index(args,1)):RAND_MAX;
        if(b<a) {
            int t=a;
            a=b;
            b=t;
        }
        return val_num(a+rand()%(b-a+1));
    }
    if(!strcmp(n,"item")) {
        if(args->len<2)return val_null();
        Value*c=g_ptr_array_index(args,0);
        Value*k=g_ptr_array_index(args,1);
        if(c->type==V_LIST) {
            int i=(int)number(k)-1;
            return(i>=0&&(guint)i<c->list->len)?val_copy(g_ptr_array_index(c->list,i)):val_null();
        }
        if(c->type==V_DICT) {
            char*s=value_string(k);
            Value*v=g_hash_table_lookup(c->dict,s);
            g_free(s);
            return val_copy(v);
        }
        return val_null();
    }
    Function*f=g_hash_table_lookup(I->functions,n);
    if(!f)return val_null();
    Scope*old=I->scope;
    Scope*sc=scope_new(old);
    I->scope=sc;
    for(guint i=0;i<f->params->len;i++) {
        const char*pn=g_ptr_array_index(f->params,i);
        Value*v=i<args->len?val_copy(g_ptr_array_index(args,i)):val_null();
        g_hash_table_replace(sc->vars,g_strdup(pn),v);
    }
    ExecResult r=execute_lines(I,f->body);
    I->scope=old;
    scope_free(sc);
    if(r.value)return r.value;
    return val_null();
}
static Value *parse_primary(Parser*p) {
    ws(p);
    if(p->s[p->p]=='"') {
        GString*s=g_string_new("");
        p->p++;
        while(p->s[p->p]&&p->s[p->p]!='"') {
            if(p->s[p->p]=='\\'&&p->s[p->p+1]) {
                p->p++;
                char c=p->s[p->p++];
                if(c=='n')g_string_append_c(s,'\n');
                else if(c=='t')g_string_append_c(s,'\t');
                else g_string_append_c(s,c);
            }else g_string_append_c(s,p->s[p->p++]);
        }
        if(p->s[p->p]=='"')p->p++;
        Value*v=val_str(s->str);
        g_string_free(s,TRUE);
        return v;
    }
    if(p->s[p->p]=='(') {
        p->p++;
        Value*v=expr_or(p);
        eat(p,")");
        return v;
    }
    if(p->s[p->p]=='[') {
        p->p++;
        Value*v=val_list();
        ws(p);
        if(eat(p,"]"))return v;
        while(1) {
            g_ptr_array_add(v->list,expr_or(p));
            if(eat(p,"]"))break;
            if(!eat(p,","))break;
        }
        return v;
    }
    if(p->s[p->p]=='{') {
        p->p++;
        Value*v=val_dict();
        ws(p);
        if(eat(p,"}"))return v;
        while(1) {
            char*k=ident(p);
            if(!k) {
                if(p->s[p->p]=='"') {
                    Value *kv=parse_primary(p);
                    k=g_strdup(kv->type==V_STR?kv->str:value_string(kv));
                    val_free(kv);
                }else break;
            }
            if(!eat(p,":")) {
                g_free(k);
                break;
            }
            g_hash_table_replace(v->dict,k,expr_or(p));
            if(eat(p,"}"))break;
            if(!eat(p,","))break;
        }
        return v;
    }
    ws(p);
    char*end=NULL;
    double d=g_ascii_strtod(p->s+p->p,&end);
    if(end&&end!=p->s+p->p) {
        p->p=end-p->s;
        return val_num(d);
    }
    char*n=ident(p);
    if(!n)return val_null();
    if(!g_ascii_strcasecmp(n,"Verdadeiro")||!g_ascii_strcasecmp(n,"true")) {
        g_free(n);
        return val_bool(TRUE);
    }
    if(!g_ascii_strcasecmp(n,"Falso")||!g_ascii_strcasecmp(n,"false")) {
        g_free(n);
        return val_bool(FALSE);
    }
    ws(p);
    if(eat(p,"(")) {
        GPtrArray*a2=g_ptr_array_new_with_free_func((GDestroyNotify)val_free);
        ws(p);
        if(!eat(p,")")) {
            while(1) {
                g_ptr_array_add(a2,expr_or(p));
                if(eat(p,")"))break;
                if(!eat(p,","))break;
            }
        }
        Value*v=call_builtin(p->I,n,a2);
        g_ptr_array_free(a2,TRUE);
        g_free(n);
        return v;
    }
    Value*v=val_copy(lookup(p->I,n));
    g_free(n);
    while(eat(p,".")) {
        char*k=ident(p);
        if(v&&v->type==V_DICT) {
            Value*x=g_hash_table_lookup(v->dict,k);
            Value*z=val_copy(x);
            val_free(v);
            v=z;
        }
        g_free(k);
    }
    return v;
}
static Value *parse_unary(Parser*p) {
    ws(p);
    if(eat(p,"nao")) {
        Value*v=parse_unary(p);
        gboolean b=!truthy(v);
        val_free(v);
        return val_bool(b);
    }
    if(eat(p,"-")) {
        Value*v=parse_unary(p);
        double n=-number(v);
        val_free(v);
        return val_num(n);
    }
    return parse_primary(p);
}
static Value *parse_mul(Parser*p) {
    Value*l=parse_unary(p);
    while(1) {
        if(eat(p,"*")) {
            Value*r=parse_unary(p);
            double x=number(l)*number(r);
            val_free(l);
            val_free(r);
            l=val_num(x);
        }else if(eat(p,"/")) {
            Value*r=parse_unary(p);
            double d=number(r);
            double x=d?number(l)/d:0;
            val_free(l);
            val_free(r);
            l=val_num(x);
        }else break;
    }
    return l;
}
static Value *parse_add(Parser*p) {
    Value*l=parse_mul(p);
    while(1) {
        if(eat(p,"+")) {
            Value*r=parse_mul(p);
            if(l->type==V_STR||r->type==V_STR) {
                char*a=value_string(l),*b=value_string(r);
                GString*s=g_string_new(a);
                g_string_append(s,b);
                val_free(l);
                val_free(r);
                g_free(a);
                g_free(b);
                l=val_str(s->str);
                g_string_free(s,TRUE);
            }else {
                double x=number(l)+number(r);
                val_free(l);
                val_free(r);
                l=val_num(x);
            }
        }else if(eat(p,"-")) {
            Value*r=parse_mul(p);
            double x=number(l)-number(r);
            val_free(l);
            val_free(r);
            l=val_num(x);
        }else break;
    }
    return l;
}
static Value *parse_cmp(Parser*p) {
    Value*l=parse_add(p);
    while(1) {
        const char*op=NULL;
        if(eat(p,"diferente"))op="!=";
        else if(eat(p,"menos+"))op="<=";
        else if(eat(p,"mais+"))op=">=";
        else if(eat(p,"menos"))op="<";
        else if(eat(p,"mais"))op=">";
        else if(eat(p,"igual"))op="==";
        else break;
        Value*r=parse_add(p);
        gboolean b=!strcmp(op,"==")?equalv(l,r):!strcmp(op,"!=")?!equalv(l,r):!strcmp(op,"<")?number(l)<number(r):!strcmp(op,">")?number(l)>number(r):!strcmp(op,"<=")?number(l)<=number(r):number(l)>=number(r);
        val_free(l);
        val_free(r);
        l=val_bool(b);
    }
    return l;
}
static Value *expr_and(Parser*p) {
    Value*l=parse_cmp(p);
    while(eat(p,"e")) {
        Value*r=parse_cmp(p);
        gboolean b=truthy(l)&&truthy(r);
        val_free(l);
        val_free(r);
        l=val_bool(b);
    }
    return l;
}
static Value *expr_or(Parser*p) {
    Value*l=expr_and(p);
    while(eat(p,"ou")) {
        Value*r=expr_and(p);
        gboolean b=truthy(l)||truthy(r);
        val_free(l);
        val_free(r);
        l=val_bool(b);
    }
    return l;
}
static Value *eval(Interpreter*I,const char*s) {
    Parser p= {
        s,0,I
    }
    ;
    return expr_or(&p);
}
static char *strip_comment(const char*src) {
    gboolean q=FALSE;
    for(size_t i=0;src[i];i++) {
        if(src[i]=='"'&& (i==0||src[i-1]!='\\'))q=!q;
        if(!q&&src[i]=='#')return g_strndup(src,i);
        if(!q&&src[i]=='-'&&src[i+1]=='-')return g_strndup(src,i);
    }
    return g_strdup(src);
}
static GPtrArray *split_args(const char*s) {
    GPtrArray*a=g_ptr_array_new_with_free_func(g_free);
    int dep=0;
    gboolean q=FALSE;
    size_t st=0;
    for(size_t i=0;s[i];i++) {
        if(s[i]=='"'&&(i==0||s[i-1]!='\\'))q=!q;
        if(q)continue;
        if(s[i]=='('||s[i]=='['||s[i]=='{')dep++;
        else if(s[i]==')'||s[i]==']'||s[i]=='}')dep--;
        else if(s[i]==','&&dep==0) {
            char*x=g_strndup(s+st,i-st);
            g_ptr_array_add(a,g_strdup(trim(x)));
            g_free(x);
            st=i+1;
        }
    }
    char*x=g_strdup(trim((char*)s+st));
    if(*x)g_ptr_array_add(a,x);
    else g_free(x);
    return a;
}
static void print_values(Interpreter*I,const char*s) {
    GPtrArray*a=split_args(s);
    for(guint i=0;i<a->len;i++) {
        Value*v=eval(I,g_ptr_array_index(a,i));
        char*x=value_string(v);
        printf("%s%s",i?" ":"",x);
        g_free(x);
        val_free(v);
    }
    putchar('\n');
    g_ptr_array_free(a,TRUE);
}
static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}
static gboolean find_matching(GPtrArray*lines,int start,int *out,int *else_at) {
    int depth=0;
    *else_at=-1;
    for(int i=start;i<(int)lines->len;i++) {
        char*s=g_ptr_array_index(lines,i);
        if(starts(s,"se ")||starts(s,"enquanto ")||starts(s,"para ")||starts(s,"funcao ")||starts(s,"janela(")||starts(s,"janela "))depth++;
        else if(!strcmp(s,"fim")) {
            if(depth==0) {
                *out=i;
                return TRUE;
            }
            depth--;
        }else if(!strcmp(s,"senao")&&depth==0)*else_at=i;
    }
    return FALSE;
}
static void define_functions(Interpreter*I,GPtrArray*lines) {
    for(int i=0;i<(int)lines->len;i++) {
        char*s=g_ptr_array_index(lines,i);
        if(starts(s,"funcao ")) {
            char*head=g_strdup(s+7);
            char*lp=strchr(head,'(');
            char*rp=lp?strrchr(lp,')'):NULL;
            if(!lp||!rp) {
                g_free(head);
                continue;
            }
            *lp=0;
            *rp=0;
            Function*f=g_new0(Function,1);
            f->params=g_ptr_array_new_with_free_func(g_free);
            f->body=g_ptr_array_new_with_free_func(g_free);
            GPtrArray*ps=split_args(lp+1);
            for(guint k=0;k<ps->len;k++)g_ptr_array_add(f->params,g_strdup(trim(g_ptr_array_index(ps,k))));
            g_ptr_array_free(ps,TRUE);
            int end,ea;
            if(find_matching(lines,i+1,&end,&ea)) {
                for(int j=i+1;j<end;j++)g_ptr_array_add(f->body,g_strdup(g_ptr_array_index(lines,j)));
                g_hash_table_replace(I->functions,g_strdup(trim(head)),f);
                i=end;
            }
            g_free(head);
        }
    }
}
static void function_free(Function*f) {
    if(!f)return;
    g_ptr_array_free(f->params,TRUE);
    g_ptr_array_free(f->body,TRUE);
    g_free(f);
}
static void button_data_destroy(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}
static void button_cb(GtkButton*b,gpointer data) {
    (void)b;
    char*name=data;
    Interpreter*I=g_object_get_data(G_OBJECT(gtk_widget_get_toplevel(GTK_WIDGET(b))),"solua-interpreter");
    if(!I)return;
    if(!strcmp(name,"sair")) {
        gtk_widget_destroy(GTK_WIDGET(gtk_widget_get_toplevel(GTK_WIDGET(b))));
        return;
    }
    Function*f=g_hash_table_lookup(I->functions,name);
    if(f) {
        Scope*old=I->scope;
        I->scope=scope_new(old);
        execute_lines(I,f->body);
        Scope*sc=I->scope;
        I->scope=old;
        scope_free(sc);
    }
}
static gboolean gui_stmt(Interpreter*I,const char*s) {
    if(!I->window)return FALSE;
    GtkWidget *container=gtk_bin_get_child(GTK_BIN(I->window));
    if(!container)return FALSE;

    if(starts(s,"texto(")) {
        char*inside=g_strdup(s+6);
        char*e=strrchr(inside,')');
        if(e)*e=0;
        GPtrArray*a=split_args(inside);
        if(a->len>=1) {
            Value *vals[4]={0};
            guint n=a->len>4?4:a->len;
            for(guint i=0;i<n;i++)vals[i]=eval(I,g_ptr_array_index(a,i));
            char *id=NULL,*text=NULL,*fg=NULL,*bg=NULL;
            if(a->len>=4) {
                id=value_string(vals[0]); text=value_string(vals[1]); fg=value_string(vals[2]); bg=value_string(vals[3]);
            } else if(a->len==3) {
                char *v0=value_string(vals[0]);
                gboolean looks_id=g_str_has_prefix(v0,"id_") ||
                    (strlen(v0)<15 && !strpbrk(v0," .!?"));
                if(looks_id) { id=g_strdup(v0); text=value_string(vals[1]); fg=value_string(vals[2]); }
                else { text=g_strdup(v0); fg=value_string(vals[1]); bg=value_string(vals[2]); }
                g_free(v0);
            } else if(a->len==2) {
                char *v0=value_string(vals[0]);
                gboolean looks_id=g_str_has_prefix(v0,"id_") ||
                    (strlen(v0)<15 && !strpbrk(v0," .!?"));
                if(looks_id) { id=g_strdup(v0); text=value_string(vals[1]); }
                else { text=g_strdup(v0); fg=value_string(vals[1]); }
                g_free(v0);
            } else {
                text=value_string(vals[0]);
            }
            GtkWidget*l=gtk_label_new(text?text:"");
            if(fg&&*fg) {
                GdkRGBA c;
                if(gdk_rgba_parse(&c,fg))gtk_widget_override_color(l,GTK_STATE_FLAG_NORMAL,&c);
            }
            if(bg&&*bg) {
                GdkRGBA c;
                if(gdk_rgba_parse(&c,bg))gtk_widget_override_background_color(l,GTK_STATE_FLAG_NORMAL,&c);
            }
            if(id)g_hash_table_replace(I->ui,g_strdup(id),g_object_ref(l));
            gtk_box_pack_start(GTK_BOX(container),l,FALSE,FALSE,5);
            gtk_widget_show_all(I->window);
            g_free(id);g_free(text);g_free(fg);g_free(bg);
            for(guint i=0;i<n;i++)val_free(vals[i]);
        }
        g_ptr_array_free(a,TRUE);g_free(inside);return TRUE;
    }

    if(starts(s,"caixa_texto(")) {
        char*inside=g_strdup(s+12);
        char*e=strrchr(inside,')');
        if(e)*e=0;
        GPtrArray*a=split_args(inside);
        if(a->len>=1) {
            Value *id=eval(I,g_ptr_array_index(a,0));
            Value *bg=a->len>1?eval(I,g_ptr_array_index(a,1)):NULL;
            Value *fg=a->len>2?eval(I,g_ptr_array_index(a,2)):NULL;
            char*sid=value_string(id);
            GtkWidget*w=gtk_entry_new();
            if(bg) { char*x=value_string(bg); GdkRGBA c; if(gdk_rgba_parse(&c,x))gtk_widget_override_background_color(w,GTK_STATE_FLAG_NORMAL,&c); g_free(x); }
            if(fg) { char*x=value_string(fg); GdkRGBA c; if(gdk_rgba_parse(&c,x))gtk_widget_override_color(w,GTK_STATE_FLAG_NORMAL,&c); g_free(x); }
            g_hash_table_replace(I->ui,g_strdup(sid),g_object_ref(w));
            gtk_box_pack_start(GTK_BOX(container),w,FALSE,FALSE,5);
            gtk_widget_show_all(I->window);
            g_free(sid);val_free(id);if(bg)val_free(bg);if(fg)val_free(fg);
        }
        g_ptr_array_free(a,TRUE);g_free(inside);return TRUE;
    }

    if(starts(s,"botao(")) {
        char*inside=g_strdup(s+6);
        char*e=strrchr(inside,')');
        if(e)*e=0;
        GPtrArray*a=split_args(inside);
        if(a->len>=2) {
            Value*t=eval(I,g_ptr_array_index(a,0));
            Value*act=eval(I,g_ptr_array_index(a,1));
            Value*bg=a->len>2?eval(I,g_ptr_array_index(a,2)):NULL;
            Value*fg=a->len>3?eval(I,g_ptr_array_index(a,3)):NULL;
            char*label=value_string(t);char*name=value_string(act);
            GtkWidget*w=gtk_button_new_with_label(label);
            if(bg) { char*x=value_string(bg); GdkRGBA c; if(gdk_rgba_parse(&c,x))gtk_widget_override_background_color(w,GTK_STATE_FLAG_NORMAL,&c); g_free(x); }
            if(fg) { char*x=value_string(fg); GdkRGBA c; if(gdk_rgba_parse(&c,x))gtk_widget_override_color(w,GTK_STATE_FLAG_NORMAL,&c); g_free(x); }
            if(*name)g_signal_connect_data(w,"clicked",G_CALLBACK(button_cb),g_strdup(name),button_data_destroy,0);
            gtk_box_pack_start(GTK_BOX(container),w,FALSE,FALSE,5);
            gtk_widget_show_all(I->window);
            g_free(label);g_free(name);val_free(t);val_free(act);if(bg)val_free(bg);if(fg)val_free(fg);
        }
        g_ptr_array_free(a,TRUE);g_free(inside);return TRUE;
    }
    return FALSE;
}
static ExecResult execute_line(Interpreter*I,GPtrArray*lines,int *ip) {
    char*raw=g_ptr_array_index(lines,*ip);
    char*s=g_strdup(trim(raw));
    ExecResult ok= {
        FLOW_OK,NULL
    }
    ;
    if(!*s||starts(s,"comentario")) {
        g_free(s);
        return ok;
    }
    if(!strcmp(s,"pare")||!strcmp(s,"quebrar")) {
        g_free(s);
        ExecResult r= {
            FLOW_BREAK,NULL
        }
        ;
        return r;
    }
    if(starts(s,"retornar")) {
        char*x=trim(s+8);
        Value*v=NULL;
        if(*x) {
            if(*x=='('&&x[strlen(x)-1]==')') {
                char*tmp=g_strndup(x+1,strlen(x)-2);
                v=eval(I,tmp);
                g_free(tmp);
            } else {
                v=eval(I,x);
            }
        } else {
            v=val_null();
        }
        g_free(s);
        ExecResult r={FLOW_RETURN,v};
        return r;
    }
    if(!strcmp(s,"limpar")||!strcmp(s,"limpar()")) {
        clear_screen();
        g_free(s);
        return ok;
    }
    if(starts(s,"escrever(")) {
        char*x=g_strdup(s+9);
        if(x[strlen(x)-1]==')')x[strlen(x)-1]=0;
        print_values(I,x);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(starts(s,"esperar(")) {
        char*x=g_strdup(s+8);
        if(x[strlen(x)-1]==')')x[strlen(x)-1]=0;
        Value*v=eval(I,x);
        g_usleep((gulong)(number(v)*1000000));
        val_free(v);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(starts(s,"ler(")) {
        char*x=g_strdup(s+4);
        if(x[strlen(x)-1]==')')x[strlen(x)-1]=0;
        GPtrArray*a=split_args(x);
        if(a->len) {
            char*vn=g_strdup(trim(g_ptr_array_index(a,0)));
            if(a->len>1) {
                Value*m=eval(I,g_ptr_array_index(a,1));
                char*ms=value_string(m);
                printf("%s",ms);
                fflush(stdout);
                g_free(ms);
                val_free(m);
            }
            char buf[4096];
            if(fgets(buf,sizeof buf,stdin)) {
                buf[strcspn(buf,"\r\n")]=0;
                char*e;
                double d=g_ascii_strtod(buf,&e);
                Value*v=(*buf&&*e==0)?val_num(d):val_str(buf);
                set_var(I,vn,v,FALSE);
            }
            g_free(vn);
        }
        g_ptr_array_free(a,TRUE);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(starts(s,"atualizar_texto(")) {
        char*x=g_strdup(s+16);
        if(x[strlen(x)-1]==')')x[strlen(x)-1]=0;
        GPtrArray*a=split_args(x);
        if(a->len>=2) {
            Value*id=eval(I,g_ptr_array_index(a,0));
            Value*tx=eval(I,g_ptr_array_index(a,1));
            char*sid=value_string(id);
            GtkWidget*w=g_hash_table_lookup(I->ui,sid);
            if(w&&GTK_IS_LABEL(w)) {
                char*text=value_string(tx);
                gtk_label_set_text(GTK_LABEL(w),text);
                g_free(text);
            }
            g_free(sid);
            val_free(id);
            val_free(tx);
        }
        g_ptr_array_free(a,TRUE);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(starts(s,"set ")||starts(s,"setl ")) {
        gboolean local=starts(s,"setl ");
        char*x=g_strdup(s+(local?5:4));
        char*comma=strchr(x,',');
        char*vn=NULL;
        char*ex=NULL;
        if(comma) {
            *comma=0;
            vn=g_strdup(trim(x));
            ex=g_strdup(trim(comma+1));
        }else {
            char*sp=strchr(x,' ');
            if(sp) {
                *sp=0;
                vn=g_strdup(trim(x));
                ex=g_strdup(trim(sp+1));
            }
        }
        if(vn&&ex) {
            if(!strncmp(ex,"pegar(",6)) {
                char*y=g_strdup(ex+6);
                if(y[strlen(y)-1]==')')y[strlen(y)-1]=0;
                Value*id=eval(I,y);
                char*sid=value_string(id);
                GtkWidget*w=g_hash_table_lookup(I->ui,sid);
                Value*v=w&&GTK_IS_ENTRY(w)?val_str(gtk_entry_get_text(GTK_ENTRY(w))):val_null();
                set_var(I,vn,v,local);
                g_free(sid);
                val_free(id);
                g_free(y);
            }else set_var(I,vn,eval(I,ex),local);
        }
        g_free(vn);
        g_free(ex);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(starts(s,"adicionar(")) {
        char*x=g_strdup(s+10);
        if(x[strlen(x)-1]==')')x[strlen(x)-1]=0;
        GPtrArray*a=split_args(x);
        if(a->len>=2) {
            Value*l=eval(I,g_ptr_array_index(a,0));
            Value*v=eval(I,g_ptr_array_index(a,1));
            if(l->type==V_LIST)g_ptr_array_add(l->list,v);
            else val_free(v);
            /* eval returns copy, so update original if variable */
            char*name=trim(g_strdup(g_ptr_array_index(a,0)));
            Value*orig=lookup(I,name);
            if(orig&&orig->type==V_LIST&&l->type==V_LIST) {
                while(orig->list->len)g_ptr_array_remove_index(orig->list,orig->list->len-1);
                for(guint z=0;z<l->list->len;z++)g_ptr_array_add(orig->list,val_copy(g_ptr_array_index(l->list,z)));
            }
            val_free(l);
        }
        g_ptr_array_free(a,TRUE);
        g_free(x);
        g_free(s);
        return ok;
    }
    if(gui_stmt(I,s)) {
        g_free(s);
        return ok;
    }
    if(starts(s,"se ")) {
        char*c=g_strdup(s+3);
        char*e=strstr(c," entao");
        if(e)*e=0;
        Value*v=eval(I,c);
        int end,ea;
        find_matching(lines,*ip+1,&end,&ea);
        if(truthy(v)) {
            int lim=ea>=0?ea:end;
            for(int j=*ip+1;j<lim;j++) {
                *ip=j;
                ExecResult r=execute_line(I,lines,ip);
                if(r.flow!=FLOW_OK) {
                    val_free(v);
                    *ip=end;
                    g_free(c);
                    g_free(s);
                    return r;
                }
            }
        }else if(ea>=0) {
            for(int j=ea+1;j<end;j++) {
                *ip=j;
                ExecResult r=execute_line(I,lines,ip);
                if(r.flow!=FLOW_OK) {
                    val_free(v);
                    *ip=end;
                    g_free(c);
                    g_free(s);
                    return r;
                }
            }
        }
        *ip=end;
        val_free(v);
        g_free(c);
        g_free(s);
        return ok;
    }
    if(starts(s,"enquanto ")) {
        char*c=g_strdup(s+9);
        char*e=strstr(c," entao");
        if(e)*e=0;
        int end,ea;
        find_matching(lines,*ip+1,&end,&ea);

        /* Executa o corpo inteiro a cada iteracao, como o Python. */
        GPtrArray*body=g_ptr_array_new_with_free_func(g_free);
        for(int j=*ip+1;j<end;j++)
            g_ptr_array_add(body,g_strdup(g_ptr_array_index(lines,j)));

        while(1) {
            Value*v=eval(I,c);
            gboolean go=truthy(v);
            val_free(v);
            if(!go)break;

            ExecResult r=execute_lines(I,body);
            if(r.flow==FLOW_BREAK) {
                val_free(r.value);
                break;
            }
            if(r.flow!=FLOW_OK) {
                *ip=end;
                g_ptr_array_free(body,TRUE);
                g_free(c);
                g_free(s);
                return r;
            }
        }

        *ip=end;
        g_ptr_array_free(body,TRUE);
        g_free(c);
        g_free(s);
        return ok;
    }
    if(starts(s,"para ")) {
        char*c=g_strdup(s+5);
        char*ent=strstr(c," em ");
        char*ate=ent?strstr(ent+4," entao"):NULL;
        int end,ea;
        find_matching(lines,*ip+1,&end,&ea);
        if(ent) {
            *ent=0;
            char*rest=ent+4;
            if(ate) {
                *ate=0;
                ate+=6;
            }
            char*var=trim(c);
            char*col=trim(rest);
            Value*v=eval(I,col);
            GPtrArray*body=g_ptr_array_new_with_free_func(g_free);
            for(int j=*ip+1;j<end;j++)
                g_ptr_array_add(body,g_strdup(g_ptr_array_index(lines,j)));

            if(v->type==V_LIST) {
                for(guint z=0;z<v->list->len;z++) {
                    set_var(I,var,val_copy(g_ptr_array_index(v->list,z)),TRUE);
                    ExecResult r=execute_lines(I,body);
                    if(r.flow==FLOW_BREAK) {
                        val_free(r.value);
                        break;
                    }
                    if(r.flow!=FLOW_OK) {
                        *ip=end;
                        g_ptr_array_free(body,TRUE);
                        val_free(v);
                        g_free(c);
                        g_free(s);
                        return r;
                    }
                }
            } else if(v->type==V_DICT) {
                GHashTableIter it;
                gpointer k,x;
                g_hash_table_iter_init(&it,v->dict);
                while(g_hash_table_iter_next(&it,&k,&x)) {
                    /* Python: for elemento in dict percorre as chaves. */
                    set_var(I,var,val_str((const char*)k),TRUE);
                    ExecResult r=execute_lines(I,body);
                    if(r.flow==FLOW_BREAK) {
                        val_free(r.value);
                        break;
                    }
                    if(r.flow!=FLOW_OK) {
                        *ip=end;
                        g_ptr_array_free(body,TRUE);
                        val_free(v);
                        g_free(c);
                        g_free(s);
                        return r;
                    }
                }
            }
            g_ptr_array_free(body,TRUE);
            val_free(v);
        }else {
            char*de=strstr(c," de ");

            if(de) {
                *de=0;

                char*var=trim(c);
                char*range=de+4;
                char*ate2=strstr(range," até ");

                if(ate2) {
                    *ate2=0;

                    char*endexpr=ate2+strlen(" até ");

                    /* Remove o "entao" do final */
                    char*entao=strstr(endexpr," entao");
                    if(entao)*entao=0;

                    Value*startv=eval(I,trim(range));
                    Value*endv=eval(I,trim(endexpr));

                    int a=(int)number(startv);
                    int b=(int)number(endv);

                    val_free(startv);
                    val_free(endv);

                    int step=(a<=b)?1:-1;

                    GPtrArray*body=g_ptr_array_new_with_free_func(g_free);

                    for(int j=*ip+1;j<end;j++)
                        g_ptr_array_add(
                            body,
                            g_strdup(g_ptr_array_index(lines,j))
                        );

                    for(int z=a;;z+=step) {
                        set_var(I,var,val_num(z),TRUE);

                        ExecResult r=execute_lines(I,body);

                        if (r.flow == FLOW_BREAK) {
                            val_free(r.value);
                            break;
                        }

                        if (r.flow == FLOW_RETURN) {
                        *ip = end;
                        g_ptr_array_free(body, TRUE);
                        g_free(c);
                        g_free(s);
                        return r; 
                        }

                        if(r.flow!=FLOW_OK) {
                            *ip=end;
                            g_ptr_array_free(body,TRUE);
                            g_free(c);
                            g_free(s);
                            return r;
                        }

                        if(z==b)
                            break;
                    }

                    g_ptr_array_free(body,TRUE);
                }
            }
        }
        *ip=end;
        g_free(c);
        g_free(s);
        return ok;
    }
    g_free(s);
    return ok;
}
static ExecResult execute_lines(Interpreter*I,GPtrArray*lines) {
    ExecResult ok= {
        FLOW_OK,NULL
    }
    ;
    for(int i=0;i<(int)lines->len;i++) {
        ExecResult r=execute_line(I,lines,&i);
        if(r.flow!=FLOW_OK)return r;
    }
    return ok;
}
static void create_window(Interpreter*I,const char*args,GPtrArray*body) {
    GPtrArray*a=split_args(args);
    if(a->len<3) {
        g_ptr_array_free(a,TRUE);
        return;
    }
    Value*t=eval(I,g_ptr_array_index(a,0));
    Value*w=eval(I,g_ptr_array_index(a,1));
    Value*h=eval(I,g_ptr_array_index(a,2));
    I->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(I->window),value_string(t));
    gtk_window_set_default_size(GTK_WINDOW(I->window),(int)number(w),(int)number(h));
    GtkWidget*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
    gtk_container_set_border_width(GTK_CONTAINER(box),8);
    gtk_container_add(GTK_CONTAINER(I->window),box);
    g_object_set_data(G_OBJECT(I->window),"solua-interpreter",I);
    gtk_widget_show_all(I->window);
    for(guint i=0;i<body->len;i++)gui_stmt(I,g_ptr_array_index(body,i));
    g_signal_connect(I->window,"destroy",G_CALLBACK(gtk_main_quit),NULL);
    val_free(t);
    val_free(w);
    val_free(h);
    g_ptr_array_free(a,TRUE);
}
static GPtrArray *load_lines(const char*file) {
    gchar*data=NULL;
    gsize len=0;
    if(!g_file_get_contents(file,&data,&len,NULL))return NULL;

    GPtrArray*l=g_ptr_array_new_with_free_func(g_free);
    char**v=g_strsplit(data,"\n",-1);
    GString*pending=NULL;
    int depth=0;
    gboolean quote=FALSE;

    for(int i=0;v[i];i++) {
        char*c=strip_comment(v[i]);
        char*s=trim(c);

        if(!*s) {
            g_free(c);
            continue;
        }

        if(!pending)pending=g_string_new("");
        if(pending->len)g_string_append_c(pending,' ');
        g_string_append(pending,s);

        for(size_t j=0;s[j];j++) {
            char ch=s[j];
            if(ch=='"' && (j==0 || s[j-1]!='\\'))quote=!quote;
            if(quote)continue;
            if(ch=='(' || ch=='[' || ch=='{')depth++;
            else if(ch==')' || ch==']' || ch=='}')depth--;
        }

        g_free(c);

        if(depth<=0) {
            char*line=g_strdup(trim(pending->str));
            if(*line)g_ptr_array_add(l,line);
            else g_free(line);
            g_string_set_size(pending,0);
            depth=0;
            quote=FALSE;
        }
    }

    if(pending) {
        char*line=g_strdup(trim(pending->str));
        if(*line)g_ptr_array_add(l,line);
        g_string_free(pending,TRUE);
    }

    g_strfreev(v);
    g_free(data);
    return l;
}

int main(int argc,char**argv) {
    if(argc!=2) {
        fprintf(stderr,"Uso: %s arquivo.soll\n",argv[0]);
        return 1;
    }
    if(!g_str_has_suffix(argv[1],".soll")) {
        fprintf(stderr,"Erro: o arquivo precisa terminar em .soll\n");
        return 1;
    }
    srand((unsigned)time(NULL));
    Interpreter I= {
        0
    }
    ;
    I.globals=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)val_free);
    I.functions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)function_free);
    I.ui=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_object_unref);
    I.scope=scope_new(NULL);
    GPtrArray*lines=load_lines(argv[1]);
    if(!lines) {
        fprintf(stderr,"Erro ao abrir %s\n",argv[1]);
        return 1;
    }
    define_functions(&I,lines);
    /* Remove function definitions from top-level execution. */
    GPtrArray*mainlines=g_ptr_array_new_with_free_func(g_free);
    for(guint i=0;i<lines->len;i++) {
        char*s=g_ptr_array_index(lines,i);
        if(starts(s,"funcao ")) {
            int end,ea;
            if(find_matching(lines,i+1,&end,&ea)) {
                i=end;
                continue;
            }
        }
        g_ptr_array_add(mainlines,g_strdup(s));
    }
    /* Handle janela blocks before ordinary execution. */
    for(int i=0;i<(int)mainlines->len;i++) {
        char*s=g_ptr_array_index(mainlines,i);
        if(starts(s,"janela(")) {
            char*x=g_strdup(s+7);
            char*e=strstr(x,") entao");
            if(e) {
                *e=0;
                int end,ea;
                find_matching(mainlines,i+1,&end,&ea);
                GPtrArray*b=g_ptr_array_new_with_free_func(g_free);
                for(int j=i+1;j<end;j++)g_ptr_array_add(b,g_strdup(g_ptr_array_index(mainlines,j)));
                if(!I.gtk_started) {
                    gtk_init(&argc,&argv);
                    I.gtk_started=TRUE;
                }
                create_window(&I,x,b);
                g_ptr_array_free(b,TRUE);
                i=end;
                continue;
            }
        }
    }
    /* Execute non-GUI statements; GUI-only statements inside janela are already consumed. */
    GPtrArray*plain=g_ptr_array_new_with_free_func(g_free);
    for(guint i=0;i<mainlines->len;i++) {
        char*s=g_ptr_array_index(mainlines,i);
        if(starts(s,"janela(")) {
            int end,ea;
            if(find_matching(mainlines,i+1,&end,&ea))i=end;
            continue;
        }
        g_ptr_array_add(plain,g_strdup(s));
    }
    ExecResult r=execute_lines(&I,plain);
    if(r.flow==FLOW_RETURN)
        val_free(r.value);

    g_ptr_array_free(plain,TRUE);
    g_ptr_array_free(mainlines,TRUE);
    g_ptr_array_free(lines,TRUE);

    /*
     * A GUI ainda usa o Interpreter através dos callbacks.
     * Portanto, o gtk_main precisa rodar ANTES de destruir I.
     */
    if(I.gtk_started)
        gtk_main();

    scope_free(I.scope);
    g_hash_table_destroy(I.ui);
    g_hash_table_destroy(I.functions);
    g_hash_table_destroy(I.globals);

    return 0;
}

#define _GNU_SOURCE     /* necessário pro FNM_CASEFOLD (-iname) e pras macros S_ISSOCK/S_ISLNK */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>
#include <sys/stat.h>

/* Guarda em um struct as opcoes que o usuario digitou com o comando */
struct Opcoes {
    int maxdepth;       /* -1 = sem limite */
    int mindepth;
    char *name_pattern;     /* -name  (NULL = não usado) */
    char *iname_pattern;    /* -iname (NULL = não usado) */
    char type;          /* -type: f, d, l... (0 = não usado) */
    int empty;          /* -empty (1 = usado) */
    char *size_arg;     /* -size  guarda o texto, ex: "+10k" */
    char *mtime_arg;    /* -mtime guarda o texto, ex: "-3" */
};

int deu_erro = 0;       /* vira 1 se algum erro acontecer */

/* =============== TESTES DE CADA OPÇÃO =============== */

/* Devolve 1 se o arquivo passa no -size (ou se -size não foi usado) */
int testa_size(struct Opcoes *op, struct stat *st)
{
    if (op->size_arg == NULL) return 1;
    return 1;       /* AINDA NÃO IMPLEMENTADO: aceita tudo */
}

/* Devolve 1 se o arquivo passa no -mtime (ou se -mtime não foi usado) */
int testa_mtime(struct Opcoes *op, struct stat *st)
{
    if (op->mtime_arg == NULL) return 1;
    return 1;       /* AINDA NÃO IMPLEMENTADO: aceita tudo */
}

/* Devolve 1 se o NOME passa no -name e no -iname */
int testa_name(struct Opcoes *op, char *nome)
{
    /* fnmatch devolve 0 quando o nome combina com o padrão (*, ?, [abc]) */
    if (op->name_pattern != NULL && fnmatch(op->name_pattern, nome, 0) != 0)
        return 0;
    /* FNM_CASEFOLD = ignora maiúscula/minúscula */
    if (op->iname_pattern != NULL && fnmatch(op->iname_pattern, nome, FNM_CASEFOLD) != 0)
        return 0;
    return 1;
}

/* Devolve 1 se o arquivo passa no -type */
int testa_type(struct Opcoes *op, struct stat *st)
{
    if (op->type == 0) return 1;

    switch (op->type) {
        case 'f': return S_ISREG(st->st_mode)  != 0;
        case 'd': return S_ISDIR(st->st_mode)  != 0;
        case 'l': return S_ISLNK(st->st_mode)  != 0;
        case 'b': return S_ISBLK(st->st_mode)  != 0;
        case 'c': return S_ISCHR(st->st_mode)  != 0;
        case 'p': return S_ISFIFO(st->st_mode) != 0;
        case 's': return S_ISSOCK(st->st_mode) != 0;
    }
    return 0;
}

/* Devolve 1 se o arquivo passa no -empty */
int testa_empty(struct Opcoes *op, char *caminho, struct stat *st)
{
    if (op->empty == 0) return 1;

    /* arquivo comum: vazio se tem 0 bytes */
    if (S_ISREG(st->st_mode))
        return st->st_size == 0;

    /* pasta: vazia se não tem nada além de "." e ".." */
    if (S_ISDIR(st->st_mode)) {
        DIR *pasta = opendir(caminho);
        if (pasta == NULL)
            return 0;   /* se não deu pra abrir, o percorre() mostra o erro quando tentar entrar */

        int vazia = 1;
        struct dirent *item;
        while ((item = readdir(pasta)) != NULL) {
            if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
                continue;
            vazia = 0;
            break;
        }
        closedir(pasta);
        return vazia;
    }

    /* link, socket, etc.: o find nunca considera vazio */
    return 0;
}

/* Percorre um caminho. Se for pasta, entra nela. */
void percorre(struct Opcoes *op, char *caminho, int nivel)
{
    struct stat st;
 
    if (lstat(caminho, &st) == -1) {
        fprintf(stderr, "find: ‘%s’: %s\n", caminho, strerror(errno));
        deu_erro = 1;
        return;
    }
 
    /* pega só o nome final: "pasta/a.txt" -> "a.txt".
       Se o caminho termina com barra ("pasta/"), o find ignora as barras do fim
       e usa "pasta" como nome, por isso trabalho numa cópia. */
    char *copia = strdup(caminho);
    if (copia == NULL) {
        perror("find");
        deu_erro = 1;
        return;
    }
    size_t tam = strlen(copia);
    while (tam > 1 && copia[tam - 1] == '/')
        copia[--tam] = '\0';

    char *nome = strrchr(copia, '/');
    if (nome == NULL || tam == 1) nome = copia;     /* tam == 1 é o caso do "/" */
    else nome = nome + 1;
 
    /* só imprime se passar em todos os testes */
    if (nivel >= op->mindepth
        && testa_name(op, nome)
        && testa_type(op, &st)
        && testa_empty(op, caminho, &st)
        && testa_size(op, &st)
        && testa_mtime(op, &st)) {
        printf("%s\n", caminho);
    }
    free(copia);
 
    /* se não é pasta, acabou */
    if (!S_ISDIR(st.st_mode)) return;
    /* se já chegou no limite de profundidade, não entra */
    if (op->maxdepth != -1 && nivel >= op->maxdepth) return;
 
    DIR *pasta = opendir(caminho);
    if (pasta == NULL) {
        fprintf(stderr, "find: ‘%s’: %s\n", caminho, strerror(errno));
        deu_erro = 1;
        return;
    }
 
    struct dirent *item;
    while ((item = readdir(pasta)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;
 
        /* monta o caminho do item: "pasta" + "/" + "nome" */
        char *novo = malloc(strlen(caminho) + strlen(item->d_name) + 2);
        if (novo == NULL) {
            perror("find");
            deu_erro = 1;
            break;
        }
        if (caminho[strlen(caminho) - 1] == '/')
            sprintf(novo, "%s%s", caminho, item->d_name);
        else
            sprintf(novo, "%s/%s", caminho, item->d_name);
 
        percorre(op, novo, nivel + 1);
        free(novo);
    }
    closedir(pasta);
}

/* Converte texto em número. Devolve -1 se não for um número válido. */
int para_numero(char *texto)
{
    if (texto[0] == '\0') return -1;
    for (int i = 0; texto[i] != '\0'; i++)
        if (texto[i] < '0' || texto[i] > '9') return -1;
    return atoi(texto);
}

int main(int argc, char *argv[])
{
    struct Opcoes op;
    op.maxdepth = -1;
    op.mindepth = 0;
    op.name_pattern = NULL;
    op.iname_pattern = NULL;
    op.type = 0;
    op.empty = 0;
    op.size_arg = NULL;
    op.mtime_arg = NULL;

    /* le as pastas que o usuario escreveu no comando */
    int i = 1;
    while (i < argc && argv[i][0] != '-')
        i++;
    int primeira = 1;
    int qtd_pastas = i - 1;
 
    /* le as opções que o usuario escreveu no comando */
    while (i < argc) {
        char *opcao = argv[i];
 
        if (strcmp(opcao, "-print") == 0) {
            /* já é o comportamento padrão */
        }
        else if (strcmp(opcao, "-empty") == 0) {
            op.empty = 1;
        }
        else if (strcmp(opcao, "-name") == 0 || strcmp(opcao, "-iname") == 0
              || strcmp(opcao, "-type") == 0 || strcmp(opcao, "-size") == 0
              || strcmp(opcao, "-mtime") == 0 || strcmp(opcao, "-maxdepth") == 0
              || strcmp(opcao, "-mindepth") == 0) {
 
            /* tratamento de erro: estas opções precisam de um valor logo depois */
            if (i + 1 >= argc) {
                fprintf(stderr, "find: missing argument to `%s'\n", opcao);
                return 1;
            }
            char *valor = argv[i + 1];
 
            if (strcmp(opcao, "-name") == 0)  op.name_pattern = valor;
            if (strcmp(opcao, "-iname") == 0) op.iname_pattern = valor;
            if (strcmp(opcao, "-size") == 0)  op.size_arg = valor;
            if (strcmp(opcao, "-mtime") == 0) op.mtime_arg = valor;
 
            if (strcmp(opcao, "-type") == 0) {
                if (strlen(valor) != 1 || strchr("bcdpfls", valor[0]) == NULL) {
                    fprintf(stderr, "find: Unknown argument to -type: %s\n", valor);
                    return 1;
                }
                op.type = valor[0];
            }
 
            if (strcmp(opcao, "-maxdepth") == 0 || strcmp(opcao, "-mindepth") == 0) {
                int n = para_numero(valor);
                if (n == -1) {
                    fprintf(stderr, "find: Expected a positive decimal integer argument to %s, but got ‘%s’\n", opcao, valor);
                    return 1;
                }
                if (strcmp(opcao, "-maxdepth") == 0) op.maxdepth = n;
                else op.mindepth = n;
            }
 
            i++;  /* pula o valor */
        }
        else if (opcao[0] == '-') {
            fprintf(stderr, "find: unknown predicate `%s'\n", opcao);
            return 1;
        }
        else {
            fprintf(stderr, "find: paths must precede expression: `%s'\n", opcao);
            return 1;
        }
        i++;
    }
 
    /* começa a busca */
    if (qtd_pastas == 0) {
        percorre(&op, ".", 0);
    } else {
        for (int k = 0; k < qtd_pastas; k++)
            percorre(&op, argv[primeira + k], 0);
    }
 
    return deu_erro;
}
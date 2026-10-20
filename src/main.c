/***************************************************************************//**

  @file         main.c

  @repository   dbsh (matrevejo/dbsh)

  @author       colpasus

  @date         2026-10-20

  @version      1.3.0

  @brief        DBSH (Database Shell)

  @description  DBSH es una shell Unix interactiva y extensible escrita en C que
                integra soporte nativo para bases de datos PostgreSQL (mediante libpq),
                motor inteligente de abreviaciones SQL (s * f tabla w x>1),
                calificación automática de tablas, sistema de alias de usuario,
                soporte completo para redirección (>, >>, <), tuberías Unix (|),
                y navegación de historial con flechas.

  @help         COMANDOS INTERNOS (BUILT-IN):
                - cd <dir>           : Cambia el directorio de trabajo actual.
                - help               : Muestra esta ayuda y la lista de comandos.
                - exit               : Cierra y termina la sesión de dbsh.
                - server <nombre>    : Consulta o define el nombre del servidor activo.
                - config             : Muestra los parámetros de conexión (.dbshell_init).
                - connect [conninfo] : Conecta a PostgreSQL (usando .dbshell_init o conninfo).
                - disconnect         : Cierra la conexión activa a PostgreSQL.
                - status             : Muestra el estado detallado de la conexión a la BBDD.
                - sql <consulta>     : Ejecuta una consulta SQL explícitamente.
                - history            : Muestra la lista de comandos ejecutados en la sesión.
                - alias [k=v]        : Lista o define alias personalizados (ej: alias srv="s * f servers").
                - unalias <nombre>   : Elimina un alias personalizado.

                ABREVIACIONES SQL RÁPIDAS (PRODUCTIVIDAD):
                - s * f <tabla> [w <cond>] [ob <col>] [l <n>]
                  Ejemplo: s * f mitabla w x>1
                  Se expande automáticamente a: SELECT * FROM mitabla WHERE mitabla.x>1;
                - tables             : Muestra todas las tablas públicas.
                - desc <tabla>       : Muestra la estructura y tipos de una tabla.
                - c <tabla>          : Muestra el número total de filas de una tabla.

                CONSULTAS SQL DIRECTAS:
                - Escribe directamente cualquier sentencia SQL estándar en el prompt
                  (SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, etc.).

                REDIRECCIÓN Y TUBERÍAS:
                - comando > archivo  : Redirige la salida sobrescribiendo el archivo.
                - comando >> archivo : Redirige la salida adjuntando al final del archivo.
                - comando < archivo  : Redirige la entrada estándar desde un archivo.
                - cmd1 | cmd2 | ...  : Conecta la salida de un comando con la entrada del siguiente.
                * Compatible con abreviaciones SQL, consultas directas y comandos de sistema.

                EDICIÓN DE LÍNEA E HISTORIAL:
                - Flechas Arriba/Abajo : Navega por el historial de comandos ejecutados.
                - Flechas Izq./Der.    : Desplaza el cursor para editar la línea actual.
                - Teclas Inicio/Fin    : Salta al principio o al final de la línea.
                - Persistencia         : El historial se guarda automáticamente en ~/.dbsh_history.

  @changelog
    - v1.3.0 (2026-10-20) [colpasus]:
      * Motor de abreviaciones SQL inteligentes (ej: 's * f mitabla w x>1' -> 'SELECT * FROM mitabla WHERE mitabla.x>1;').
      * Calificación automática de columnas con el nombre de la tabla en cláusulas WHERE.
      * Atajos rápidos para inspección de BBDD: 'tables' (listar tablas), 'desc <tabla>' (estructura) y 'c <tabla>' (conteo).
      * Soporte para alias de usuario personalizados mediante built-ins 'alias' / 'unalias' y carga en .dbshell_init.
      * Desambiguación inteligente entre operadores de comparación SQL ('>', '<', '>=', '<=') y redirección shell.
      * Notificación visual '[dbsh:sql]' mostrando la consulta SQL expandida antes de su ejecución.
    - v1.2.0 (2026-10-20) [colpasus]:
      * Implementado historial interactivo navegable con flechas arriba (Up) y abajo (Down).
      * Desplazamiento de cursor con flechas izquierda/derecha, teclas Inicio/Fin y Delete.
      * Persistencia automática del historial en el archivo ~/.dbsh_history.
      * Añadido nuevo comando built-in 'history' para visualizar el historial numerado.
      * Detección de terminal TTY con fallback transparente a streams/tuberías.
    - v1.1.0 (2026-10-20) [colpasus]:
      * Soporte completo para redirección estándar de salida ('>' truncar, '>>' append).
      * Soporte para redirección estándar de entrada ('<').
      * Soporte para tuberías UNIX ('|') entre comandos arbitrarios.
      * Compatibilidad de tuberías y redirecciones con consultas SQL (ej: SELECT ... | grep).
      * Agregado encabezado informativo detallado con autor, repo, ayuda e historial.
    - v1.0.0 (2026-10-20) [colpasus]:
      * Integración nativa con PostgreSQL vía libpq (libpq-fe.h).
      * Detección y ejecución automática de sentencias SQL en el prompt.
      * Salida de consultas formateada en tablas alineadas con conteo de filas.
      * Archivo de inicialización .dbshell_init y auto-conexión al inicio.
      * Comandos de base de datos: connect, disconnect, status, sql, server, config.
      * Transición de base LSH a DBSH (Database Shell).

*******************************************************************************/

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <libpq-fe.h>

/*
  Global PostgreSQL connection handle
 */
static PGconn *db_conn = NULL;

/*
  Configuration structure for DBSH
 */
typedef struct {
  char server[64];
  char db_host[64];
  char db_port[16];
  char db_name[64];
  char db_user[64];
  char db_password[64];
  int auto_connect;
} dbsh_config_t;

static dbsh_config_t config = {
  .server = "localhost",
  .db_host = "",
  .db_port = "5432",
  .db_name = "dbsh_sandbox",
  .db_user = "colpasus",
  .db_password = "",
  .auto_connect = 1
};

/*
  Function Declarations for builtin shell commands:
 */
int dbsh_cd(char **args);
int dbsh_help(char **args);
int dbsh_exit(char **args);
int dbsh_server(char **args);
int dbsh_config(char **args);
int dbsh_connect(char **args);
int dbsh_disconnect(char **args);
int dbsh_status(char **args);
int dbsh_sql(char **args);
int dbsh_history(char **args);
int dbsh_alias(char **args);
int dbsh_unalias(char **args);

char *dbsh_read_line(const char *prompt);
char **dbsh_split_line(char *line);
int dbsh_launch(char **args);
int dbsh_execute_single(char *cmd_str);
int dbsh_execute_pipeline(char *line);
void dbsh_loop(void);

/*
  Alias Management System
 */
#define DBSH_ALIAS_MAX 64
typedef struct {
  char name[32];
  char command[512];
} dbsh_alias_t;

static dbsh_alias_t dbsh_aliases[DBSH_ALIAS_MAX];
static int dbsh_alias_count = 0;

const char *dbsh_find_alias(const char *name);
int dbsh_set_alias(const char *name, const char *val);
char *dbsh_expand_shorthand(const char *input);

/*
  History Management System
 */
#define DBSH_HIST_MAX 1000
static char *dbsh_history_list[DBSH_HIST_MAX];
static int dbsh_history_count = 0;

void dbsh_add_history(const char *cmd);
void dbsh_load_history(void);
void dbsh_save_history(void);

/*
  List of builtin commands, followed by their corresponding functions.
 */
char *builtin_str[] = {
  "cd",
  "help",
  "exit",
  "server",
  "config",
  "connect",
  "disconnect",
  "status",
  "sql",
  "history",
  "alias",
  "unalias"
};

int (*builtin_func[]) (char **) = {
  &dbsh_cd,
  &dbsh_help,
  &dbsh_exit,
  &dbsh_server,
  &dbsh_config,
  &dbsh_connect,
  &dbsh_disconnect,
  &dbsh_status,
  &dbsh_sql,
  &dbsh_history,
  &dbsh_alias,
  &dbsh_unalias
};

int dbsh_num_builtins() {
  return sizeof(builtin_str) / sizeof(char *);
}

static char *trim_whitespace(char *str)
{
  char *end;
  while (isspace((unsigned char)*str)) str++;
  if (*str == 0) return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;
  end[1] = '\0';
  return str;
}

void dbsh_print_prompt(void)
{
  printf("dbsh> ");
  fflush(stdout);
}

int dbsh_set_alias(const char *name, const char *val)
{
  char clean_val[512] = "";
  strncpy(clean_val, val, sizeof(clean_val) - 1);
  clean_val[sizeof(clean_val) - 1] = '\0';
  char *v = trim_whitespace(clean_val);
  size_t len = strlen(v);
  if (len >= 2 && ((v[0] == '"' && v[len - 1] == '"') || (v[0] == '\'' && v[len - 1] == '\''))) {
    v[len - 1] = '\0';
    v++;
  }

  for (int i = 0; i < dbsh_alias_count; i++) {
    if (strcmp(dbsh_aliases[i].name, name) == 0) {
      strncpy(dbsh_aliases[i].command, v, sizeof(dbsh_aliases[i].command) - 1);
      dbsh_aliases[i].command[sizeof(dbsh_aliases[i].command) - 1] = '\0';
      return 1;
    }
  }

  if (dbsh_alias_count < DBSH_ALIAS_MAX) {
    strncpy(dbsh_aliases[dbsh_alias_count].name, name, sizeof(dbsh_aliases[dbsh_alias_count].name) - 1);
    dbsh_aliases[dbsh_alias_count].name[sizeof(dbsh_aliases[dbsh_alias_count].name) - 1] = '\0';
    strncpy(dbsh_aliases[dbsh_alias_count].command, v, sizeof(dbsh_aliases[dbsh_alias_count].command) - 1);
    dbsh_aliases[dbsh_alias_count].command[sizeof(dbsh_aliases[dbsh_alias_count].command) - 1] = '\0';
    dbsh_alias_count++;
    return 1;
  }
  return 0;
}

void dbsh_load_config(void)
{
  const char *config_paths[2];
  char home_config[512] = "";
  char *home = getenv("HOME");

  config_paths[0] = ".dbshell_init";
  if (home != NULL) {
    snprintf(home_config, sizeof(home_config), "%s/.dbshell_init", home);
    config_paths[1] = home_config;
  } else {
    config_paths[1] = NULL;
  }

  FILE *fp = NULL;
  const char *loaded_path = NULL;

  for (int i = 0; i < 2; i++) {
    if (config_paths[i] != NULL) {
      fp = fopen(config_paths[i], "r");
      if (fp != NULL) {
        loaded_path = config_paths[i];
        break;
      }
    }
  }

  if (fp == NULL) {
    return;
  }

  printf("[dbsh] Archivo de inicialización cargado: %s\n", loaded_path);

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    char *trimmed = trim_whitespace(line);
    if (*trimmed == '#' || *trimmed == '\0') {
      continue;
    }

    if (strncmp(trimmed, "alias ", 6) == 0) {
      char *a = trim_whitespace(trimmed + 6);
      char *eq = strchr(a, '=');
      if (eq != NULL) {
        *eq = '\0';
        char *aname = trim_whitespace(a);
        char *aval = trim_whitespace(eq + 1);
        if (strlen(aname) > 0 && strlen(aval) > 0) {
          dbsh_set_alias(aname, aval);
        }
      }
      continue;
    }

    char *eq = strchr(trimmed, '=');
    if (!eq) continue;

    *eq = '\0';
    char *key = trim_whitespace(trimmed);
    char *val = trim_whitespace(eq + 1);

    if (strcmp(key, "SERVER") == 0) {
      strncpy(config.server, val, sizeof(config.server) - 1);
      config.server[sizeof(config.server) - 1] = '\0';
    } else if (strcmp(key, "DB_HOST") == 0 || strcmp(key, "HOST") == 0) {
      strncpy(config.db_host, val, sizeof(config.db_host) - 1);
      config.db_host[sizeof(config.db_host) - 1] = '\0';
    } else if (strcmp(key, "DB_PORT") == 0 || strcmp(key, "PORT") == 0) {
      strncpy(config.db_port, val, sizeof(config.db_port) - 1);
      config.db_port[sizeof(config.db_port) - 1] = '\0';
    } else if (strcmp(key, "DB_NAME") == 0 || strcmp(key, "DBNAME") == 0) {
      strncpy(config.db_name, val, sizeof(config.db_name) - 1);
      config.db_name[sizeof(config.db_name) - 1] = '\0';
    } else if (strcmp(key, "DB_USER") == 0 || strcmp(key, "USER") == 0) {
      strncpy(config.db_user, val, sizeof(config.db_user) - 1);
      config.db_user[sizeof(config.db_user) - 1] = '\0';
    } else if (strcmp(key, "DB_PASSWORD") == 0 || strcmp(key, "PASSWORD") == 0) {
      strncpy(config.db_password, val, sizeof(config.db_password) - 1);
      config.db_password[sizeof(config.db_password) - 1] = '\0';
    } else if (strcmp(key, "AUTO_CONNECT") == 0) {
      config.auto_connect = atoi(val);
    }
  }

  fclose(fp);

  if (config.auto_connect) {
    printf("[dbsh] Auto-connect habilitado para base de datos '%s'.\n", config.db_name);
  }
}

/*
  Builtin function implementations.
*/

/**
   @brief Builtin command: change directory.
   @param args List of args.  args[0] is "cd".  args[1] is the directory.
   @return Always returns 1, to continue executing.
 */
int dbsh_cd(char **args)
{
  if (args[1] == NULL) {
    fprintf(stderr, "dbsh: expected argument to \"cd\"\n");
  } else {
    if (chdir(args[1]) != 0) {
      perror("dbsh");
    }
  }
  return 1;
}

/**
   @brief Builtin command: print help.
   @param args List of args.  Not examined.
   @return Always returns 1, to continue executing.
 */
int dbsh_help(char **args)
{
  (void)args;
  int i;
  printf("DBSH (Database Shell) - v1.3.0\n");
  printf("Escribe comandos de shell, sentencias SQL, abreviaciones o tuberías y presiona Enter.\n\n");
  printf("Comandos built-in:\n");
  for (i = 0; i < dbsh_num_builtins(); i++) {
    printf("  %s\n", builtin_str[i]);
  }
  printf("\nAbreviaciones SQL Rápidas (Productividad):\n");
  printf("  s * f <tabla> [w <cond>] [ob <col>] [l <n>]\n");
  printf("    Ejemplo: s * f mitabla w x>1\n");
  printf("    -> SELECT * FROM mitabla WHERE mitabla.x>1;\n");
  printf("  tables              : Lista las tablas públicas\n");
  printf("  desc <tabla>        : Describe columnas y tipos de datos\n");
  printf("  c <tabla>           : Muestra el total de filas de la tabla\n");
  printf("\nConsultas SQL directas:\n");
  printf("  SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, etc.\n");
  printf("\nRedirección y Tuberías:\n");
  printf("  > archivo   (sobrescribir salida)     >> archivo  (adjuntar salida)\n");
  printf("  < archivo   (redireccionar entrada)    |          (tubería entre comandos)\n");
  printf("\nHistorial y Alias:\n");
  printf("  Flechas Arriba / Abajo : Recuperar comandos anteriores del historial\n");
  printf("  alias nombre=\"comando\"  : Define un alias personalizado\n");
  printf("  unalias nombre         : Elimina un alias\n\n");
  return 1;
}

/**
   @brief Builtin command: exit.
   @param args List of args.  Not examined.
   @return Always returns 0, to terminate execution.
 */
int dbsh_exit(char **args)
{
  (void)args;
  return 0;
}

/**
   @brief Builtin command: server.
   @param args List of args. args[1] is new server name.
   @return Always returns 1, to continue executing.
 */
int dbsh_server(char **args)
{
  if (args[1] == NULL) {
    printf("Servidor actual: %s\n", config.server);
    printf("Uso: server <nombre_servidor>\n");
  } else {
    strncpy(config.server, args[1], sizeof(config.server) - 1);
    config.server[sizeof(config.server) - 1] = '\0';
    printf("Servidor actualizado a: %s\n", config.server);
  }
  return 1;
}

/**
   @brief Builtin command: config.
   @param args List of args.
   @return Always returns 1, to continue executing.
 */
int dbsh_config(char **args)
{
  (void)args;
  printf("\n=== Configuración DBSH (.dbshell_init) ===\n");
  printf("  Servidor:     %s\n", config.server);
  printf("  DB Host:      %s\n", strlen(config.db_host) > 0 ? config.db_host : "(socket local)");
  printf("  DB Port:      %s\n", config.db_port);
  printf("  DB Name:      %s\n", config.db_name);
  printf("  DB User:      %s\n", config.db_user);
  printf("  DB Password:  %s\n", strlen(config.db_password) > 0 ? "******" : "(no configurada)");
  printf("  Auto-connect: %s\n", config.auto_connect ? "Habilitado (1)" : "Deshabilitado (0)");
  printf("==========================================\n\n");
  return 1;
}

int dbsh_do_connect(const char *conninfo)
{
  if (db_conn != NULL) {
    PQfinish(db_conn);
    db_conn = NULL;
  }

  db_conn = PQconnectdb(conninfo);

  if (PQstatus(db_conn) != CONNECTION_OK) {
    fprintf(stderr, "[dbsh] Error de conexión: %s", PQerrorMessage(db_conn));
    PQfinish(db_conn);
    db_conn = NULL;
    return 0;
  }

  printf("[dbsh] Conectado a PostgreSQL: db='%s', user='%s'.\n",
         PQdb(db_conn), PQuser(db_conn));
  return 1;
}

void dbsh_auto_connect(void)
{
  char conninfo[512] = "";
  if (strlen(config.db_host) > 0) {
    if (strlen(config.db_password) > 0) {
      snprintf(conninfo, sizeof(conninfo), "host=%s port=%s dbname=%s user=%s password=%s",
               config.db_host, config.db_port, config.db_name, config.db_user, config.db_password);
    } else {
      snprintf(conninfo, sizeof(conninfo), "host=%s port=%s dbname=%s user=%s",
               config.db_host, config.db_port, config.db_name, config.db_user);
    }
  } else {
    if (strlen(config.db_password) > 0) {
      snprintf(conninfo, sizeof(conninfo), "port=%s dbname=%s user=%s password=%s",
               config.db_port, config.db_name, config.db_user, config.db_password);
    } else {
      snprintf(conninfo, sizeof(conninfo), "port=%s dbname=%s user=%s",
               config.db_port, config.db_name, config.db_user);
    }
  }
  dbsh_do_connect(conninfo);
}

int dbsh_connect(char **args)
{
  if (args[1] == NULL) {
    dbsh_auto_connect();
  } else {
    char custom_conn[1024] = "";
    for (int i = 1; args[i] != NULL; i++) {
      if (i > 1) strncat(custom_conn, " ", sizeof(custom_conn) - strlen(custom_conn) - 1);
      strncat(custom_conn, args[i], sizeof(custom_conn) - strlen(custom_conn) - 1);
    }
    dbsh_do_connect(custom_conn);
  }
  return 1;
}

int dbsh_disconnect(char **args)
{
  (void)args;
  if (db_conn == NULL) {
    printf("[dbsh] No hay conexión activa a PostgreSQL.\n");
  } else {
    PQfinish(db_conn);
    db_conn = NULL;
    printf("[dbsh] Desconectado de PostgreSQL.\n");
  }
  return 1;
}

int dbsh_status(char **args)
{
  (void)args;
  printf("\n=== Estado de Conexión DBSH (PostgreSQL) ===\n");
  if (db_conn == NULL || PQstatus(db_conn) != CONNECTION_OK) {
    printf("  Estado:        Desconectado\n");
    printf("  Sugerencia:    Usa 'connect' para conectar a la BBDD\n");
  } else {
    const char *host = PQhost(db_conn);
    printf("  Estado:        Conectado (CONNECTION_OK)\n");
    printf("  Base de datos: %s\n", PQdb(db_conn));
    printf("  Usuario:       %s\n", PQuser(db_conn));
    printf("  Servidor/Host: %s\n", (host && strlen(host) > 0) ? host : "Socket UNIX local (/var/run/postgresql)");
    printf("  Puerto:        %s\n", PQport(db_conn));
    printf("  Versión Serv.: %d\n", PQserverVersion(db_conn));
  }
  printf("============================================\n\n");
  return 1;
}

int dbsh_execute_sql(const char *sql_query)
{
  if (db_conn == NULL || PQstatus(db_conn) != CONNECTION_OK) {
    fprintf(stderr, "dbsh: no hay conexión activa a PostgreSQL. Usa 'connect' primero.\n");
    return 1;
  }

  PGresult *res = PQexec(db_conn, sql_query);
  if (res == NULL) {
    fprintf(stderr, "dbsh: error al ejecutar consulta: %s\n", PQerrorMessage(db_conn));
    return 1;
  }

  ExecStatusType res_status = PQresultStatus(res);

  if (res_status == PGRES_TUPLES_OK) {
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    int *col_widths = malloc(cols * sizeof(int));
    if (!col_widths) {
      PQclear(res);
      return 1;
    }

    for (int c = 0; c < cols; c++) {
      col_widths[c] = strlen(PQfname(res, c));
      if (col_widths[c] < 6) col_widths[c] = 6;
      for (int r = 0; r < rows; r++) {
        int len = strlen(PQgetvalue(res, r, c));
        if (len > col_widths[c]) {
          col_widths[c] = len;
        }
      }
      if (col_widths[c] > 50) col_widths[c] = 50;
    }

    printf("\n");
    for (int c = 0; c < cols; c++) {
      printf(" %-*s ", col_widths[c], PQfname(res, c));
      if (c < cols - 1) printf("|");
    }
    printf("\n");

    for (int c = 0; c < cols; c++) {
      for (int w = 0; w < col_widths[c] + 2; w++) putchar('-');
      if (c < cols - 1) putchar('+');
    }
    printf("\n");

    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        char *val = PQgetvalue(res, r, c);
        if (PQgetisnull(res, r, c)) val = "NULL";
        printf(" %-*s ", col_widths[c], val);
        if (c < cols - 1) printf("|");
      }
      printf("\n");
    }

    printf("(%d %s)\n\n", rows, rows == 1 ? "fila" : "filas");
    free(col_widths);

  } else if (res_status == PGRES_COMMAND_OK) {
    const char *cmd_status = PQcmdStatus(res);
    printf("%s\n", cmd_status && strlen(cmd_status) > 0 ? cmd_status : "OK");
  } else if (res_status == PGRES_EMPTY_QUERY) {
    // Consulta vacía
  } else {
    fprintf(stderr, "Error SQL: %s", PQerrorMessage(db_conn));
  }

  PQclear(res);
  return 1;
}

static int is_sql_command(const char *cmd)
{
  if (cmd == NULL) return 0;
  const char *sql_keywords[] = {
    "select", "insert", "update", "delete", "create", "drop",
    "alter", "truncate", "show", "explain", "with", "begin",
    "commit", "rollback", "grant", "revoke",
    NULL
  };

  for (int i = 0; sql_keywords[i] != NULL; i++) {
    if (strcasecmp(cmd, sql_keywords[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

int dbsh_sql(char **args)
{
  if (args[1] == NULL) {
    printf("Uso: sql <consulta_sql>\n");
    printf("Nota: También puedes escribir consultas SQL directamente (ej: SELECT * FROM servers;)\n");
    return 1;
  }

  char query[2048] = "";
  for (int i = 1; args[i] != NULL; i++) {
    if (i > 1) strncat(query, " ", sizeof(query) - strlen(query) - 1);
    strncat(query, args[i], sizeof(query) - strlen(query) - 1);
  }
  return dbsh_execute_sql(query);
}

int dbsh_history(char **args)
{
  (void)args;
  printf("\n=== Historial de Comandos DBSH ===\n");
  if (dbsh_history_count == 0) {
    printf("  (Historial vacío)\n");
  } else {
    for (int i = 0; i < dbsh_history_count; i++) {
      printf("  %3d  %s\n", i + 1, dbsh_history_list[i]);
    }
  }
  printf("==================================\n\n");
  return 1;
}

void dbsh_add_history(const char *cmd)
{
  if (!cmd || strlen(cmd) == 0) return;
  // Evitar duplicar el comando inmediato anterior
  if (dbsh_history_count > 0 && strcmp(dbsh_history_list[dbsh_history_count - 1], cmd) == 0) {
    return;
  }

  if (dbsh_history_count < DBSH_HIST_MAX) {
    dbsh_history_list[dbsh_history_count++] = strdup(cmd);
  } else {
    free(dbsh_history_list[0]);
    memmove(&dbsh_history_list[0], &dbsh_history_list[1], sizeof(char *) * (DBSH_HIST_MAX - 1));
    dbsh_history_list[DBSH_HIST_MAX - 1] = strdup(cmd);
  }
}

void dbsh_load_history(void)
{
  char *home = getenv("HOME");
  if (!home) return;

  char path[512];
  snprintf(path, sizeof(path), "%s/.dbsh_history", home);

  FILE *fp = fopen(path, "r");
  if (!fp) return;

  char line[2048];
  while (fgets(line, sizeof(line), fp)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    if (len > 0) {
      dbsh_add_history(line);
    }
  }
  fclose(fp);
}

void dbsh_save_history(void)
{
  char *home = getenv("HOME");
  if (!home) return;

  char path[512];
  snprintf(path, sizeof(path), "%s/.dbsh_history", home);

  FILE *fp = fopen(path, "w");
  if (!fp) return;

  for (int i = 0; i < dbsh_history_count; i++) {
    fprintf(fp, "%s\n", dbsh_history_list[i]);
  }
  fclose(fp);
}

const char *dbsh_find_alias(const char *name)
{
  for (int i = 0; i < dbsh_alias_count; i++) {
    if (strcmp(dbsh_aliases[i].name, name) == 0) {
      return dbsh_aliases[i].command;
    }
  }
  return NULL;
}

int dbsh_alias(char **args)
{
  if (args[1] == NULL) {
    printf("\n=== Alias de Usuario en DBSH ===\n");
    if (dbsh_alias_count == 0) {
      printf("  (No hay alias definidos. Uso: alias nombre=\"comando\")\n");
    } else {
      for (int i = 0; i < dbsh_alias_count; i++) {
        printf("  alias %s='%s'\n", dbsh_aliases[i].name, dbsh_aliases[i].command);
      }
    }
    printf("================================\n\n");
    return 1;
  }

  char *eq = strchr(args[1], '=');
  if (eq == NULL) {
    const char *val = dbsh_find_alias(args[1]);
    if (val) {
      printf("alias %s='%s'\n", args[1], val);
    } else {
      fprintf(stderr, "dbsh: alias '%s' no encontrado\n", args[1]);
    }
    return 1;
  }

  *eq = '\0';
  char *name = args[1];
  char *val = eq + 1;

  char full_val[512] = "";
  if (val[0] == '\'' || val[0] == '"') {
    val++;
    strncat(full_val, val, sizeof(full_val) - 1);
    for (int i = 2; args[i] != NULL; i++) {
      strncat(full_val, " ", sizeof(full_val) - strlen(full_val) - 1);
      strncat(full_val, args[i], sizeof(full_val) - strlen(full_val) - 1);
    }
    size_t l = strlen(full_val);
    if (l > 0 && (full_val[l - 1] == '\'' || full_val[l - 1] == '"')) {
      full_val[l - 1] = '\0';
    }
  } else {
    strncat(full_val, val, sizeof(full_val) - 1);
    for (int i = 2; args[i] != NULL; i++) {
      strncat(full_val, " ", sizeof(full_val) - strlen(full_val) - 1);
      strncat(full_val, args[i], sizeof(full_val) - strlen(full_val) - 1);
    }
  }

  if (dbsh_set_alias(name, full_val)) {
    printf("[dbsh] Alias registrado: %s -> '%s'\n", name, full_val);
  } else {
    fprintf(stderr, "dbsh: límite de alias alcanzado (%d)\n", DBSH_ALIAS_MAX);
  }

  return 1;
}

int dbsh_unalias(char **args)
{
  if (args[1] == NULL) {
    fprintf(stderr, "dbsh: uso: unalias <nombre_alias>\n");
    return 1;
  }

  for (int i = 0; i < dbsh_alias_count; i++) {
    if (strcmp(dbsh_aliases[i].name, args[1]) == 0) {
      for (int j = i; j < dbsh_alias_count - 1; j++) {
        dbsh_aliases[j] = dbsh_aliases[j + 1];
      }
      dbsh_alias_count--;
      printf("[dbsh] Alias '%s' eliminado.\n", args[1]);
      return 1;
    }
  }

  fprintf(stderr, "dbsh: alias '%s' no encontrado\n", args[1]);
  return 1;
}

static int is_sql_operator(const char *t)
{
  if (!t) return 0;
  return (strcmp(t, "=") == 0 || strcmp(t, ">") == 0 || strcmp(t, "<") == 0 ||
          strcmp(t, ">=") == 0 || strcmp(t, "<=") == 0 || strcmp(t, "!=") == 0 ||
          strcmp(t, "<>") == 0 || strcasecmp(t, "like") == 0 || strcasecmp(t, "ilike") == 0 ||
          strcasecmp(t, "is") == 0 || strcasecmp(t, "in") == 0 || strcasecmp(t, "between") == 0 ||
          strcmp(t, "~") == 0 || strcmp(t, "!~") == 0);
}

static void qualify_table_col(const char *t, const char *next_t, const char *table, char *out, size_t out_sz)
{
  if (!table || strlen(table) == 0) {
    snprintf(out, out_sz, "%s", t);
    return;
  }

  // No cualificar palabras clave lógicas o literales comunes de SQL
  if (strcasecmp(t, "and") == 0 || strcasecmp(t, "or") == 0 || strcasecmp(t, "not") == 0 ||
      strcasecmp(t, "is") == 0 || strcasecmp(t, "in") == 0 || strcasecmp(t, "null") == 0 ||
      strcasecmp(t, "between") == 0 || strcasecmp(t, "like") == 0 || strcasecmp(t, "ilike") == 0 ||
      strcasecmp(t, "true") == 0 || strcasecmp(t, "false") == 0) {
    snprintf(out, out_sz, "%s", t);
    return;
  }

  // Si t contiene el operador directamente, ej: x>1 o status='online'
  char *ops = "><=!~";
  const char *op_pos = strpbrk(t, ops);
  if (op_pos != NULL) {
    int id_len = op_pos - t;
    char id[64];
    if (id_len > 0 && id_len < 63) {
      strncpy(id, t, id_len);
      id[id_len] = '\0';
      if (strchr(id, '.') == NULL && strlen(id) > 0 && isalpha((unsigned char)id[0])) {
        snprintf(out, out_sz, "%s.%s%s", table, id, op_pos);
        return;
      }
    }
  }

  // Si el operador está en el siguiente token, ej: "x" seguido de ">"
  if (next_t != NULL && is_sql_operator(next_t)) {
    if (strchr(t, '.') == NULL && strlen(t) > 0 && isalpha((unsigned char)t[0])) {
      snprintf(out, out_sz, "%s.%s", table, t);
      return;
    }
  }

  snprintf(out, out_sz, "%s", t);
}

char *dbsh_expand_shorthand(const char *input)
{
  char line[2048];
  strncpy(line, input, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  char *p = line;
  while (isspace((unsigned char)*p)) p++;
  if (*p == '\0') return NULL;

  char first[32] = "";
  sscanf(p, "%31s", first);

  // Atajos directos de inspección
  if (strcasecmp(first, "tables") == 0 || strcasecmp(first, "\\dt") == 0) {
    return strdup("SELECT table_name FROM information_schema.tables WHERE table_schema = 'public' ORDER BY table_name;");
  }
  if (strcasecmp(first, "desc") == 0 || (strcasecmp(first, "d") == 0 && strlen(p) > 2)) {
    char tbl[64] = "";
    sscanf(p + strlen(first), "%63s", tbl);
    if (strlen(tbl) > 0) {
      char sql[512];
      snprintf(sql, sizeof(sql),
               "SELECT column_name, data_type, is_nullable FROM information_schema.columns WHERE table_name = '%s' ORDER BY ordinal_position;",
               tbl);
      return strdup(sql);
    }
  }
  if (strcasecmp(first, "c") == 0 || strcasecmp(first, "count") == 0) {
    char tbl[64] = "";
    sscanf(p + strlen(first), "%63s", tbl);
    if (strlen(tbl) > 0) {
      char sql[256];
      snprintf(sql, sizeof(sql), "SELECT count(*) AS total_rows FROM %s;", tbl);
      return strdup(sql);
    }
  }

  // Triggers de abreviaciones SQL (s, i, u, del)
  if (strcasecmp(first, "s") != 0 && strcasecmp(first, "i") != 0 &&
      strcasecmp(first, "u") != 0 && strcasecmp(first, "del") != 0) {
    return NULL;
  }

  // Tokenización respetando comillas
  char *tokens[128];
  int token_count = 0;
  int len = strlen(p);
  char quote = 0;
  int start = -1;

  for (int i = 0; i <= len; i++) {
    char c = p[i];
    if (!quote && (c == '\'' || c == '"')) {
      quote = c;
      if (start == -1) start = i;
    } else if (quote && c == quote) {
      quote = 0;
    } else if (!quote && (isspace((unsigned char)c) || c == '\0')) {
      if (start != -1) {
        p[i] = '\0';
        tokens[token_count++] = p + start;
        start = -1;
        if (token_count >= 127) break;
      }
    } else {
      if (start == -1) start = i;
    }
  }

  if (token_count == 0) return NULL;

  // Paso 1: Localizar la tabla principal tras 'f'
  char main_table[64] = "";
  for (int i = 0; i < token_count; i++) {
    if (strcasecmp(tokens[i], "f") == 0 && i + 1 < token_count) {
      strncpy(main_table, tokens[i + 1], sizeof(main_table) - 1);
      main_table[sizeof(main_table) - 1] = '\0';
      break;
    }
  }

  // Paso 2: Construir la consulta SQL expandida
  char buf[4096] = "";
  int in_where = 0;

  for (int i = 0; i < token_count; i++) {
    const char *t = tokens[i];
    if (i > 0) strcat(buf, " ");

    if (i == 0 && strcasecmp(t, "s") == 0) {
      strcat(buf, "SELECT");
    } else if (i == 0 && strcasecmp(t, "i") == 0) {
      strcat(buf, "INSERT INTO");
    } else if (i == 0 && strcasecmp(t, "u") == 0) {
      strcat(buf, "UPDATE");
    } else if (i == 0 && strcasecmp(t, "del") == 0) {
      strcat(buf, "DELETE FROM");
    } else if (strcasecmp(t, "f") == 0) {
      strcat(buf, "FROM");
      in_where = 0;
    } else if (strcasecmp(t, "w") == 0) {
      strcat(buf, "WHERE");
      in_where = 1;
    } else if (strcasecmp(t, "ob") == 0 || strcasecmp(t, "o") == 0) {
      strcat(buf, "ORDER BY");
      in_where = 0;
    } else if (strcasecmp(t, "gb") == 0 || strcasecmp(t, "g") == 0) {
      strcat(buf, "GROUP BY");
      in_where = 0;
    } else if (strcasecmp(t, "l") == 0 || strcasecmp(t, "lim") == 0) {
      strcat(buf, "LIMIT");
      in_where = 0;
    } else if (strcasecmp(t, "j") == 0) {
      strcat(buf, "JOIN");
      in_where = 0;
    } else if (strcasecmp(t, "lj") == 0) {
      strcat(buf, "LEFT JOIN");
      in_where = 0;
    } else if (strcasecmp(t, "ij") == 0) {
      strcat(buf, "INNER JOIN");
      in_where = 0;
    } else if (strcasecmp(t, "v") == 0) {
      strcat(buf, "VALUES");
      in_where = 0;
    } else {
      if (in_where) {
        const char *next_t = (i + 1 < token_count) ? tokens[i + 1] : NULL;
        char qual[256];
        qualify_table_col(t, next_t, main_table, qual, sizeof(qual));
        strcat(buf, qual);
      } else {
        strcat(buf, t);
      }
    }
  }

  if (strlen(buf) > 0 && buf[strlen(buf) - 1] != ';') {
    strcat(buf, ";");
  }

  return strdup(buf);
}

/**
  @brief Launch a program and wait for it to terminate.
  @param args Null terminated list of arguments (including program).
  @return Always returns 1, to continue execution.
 */
int dbsh_launch(char **args)
{
  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0) {
    // Child process
    if (execvp(args[0], args) == -1) {
      perror("dbsh");
    }
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    // Error forking
    perror("dbsh");
  } else {
    // Parent process
    do {
      waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  return 1;
}

static void trim_inplace(char *s)
{
  char *p = s;
  while (isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[--len] = '\0';
  }
}

static int is_sql_comparison_operator(const char *cmd, int i)
{
  if (i > 0 && (cmd[i - 1] == '<' || cmd[i - 1] == '=' || cmd[i - 1] == '!' || cmd[i - 1] == '~')) return 1;
  if (cmd[i + 1] == '=' || (cmd[i] == '<' && cmd[i + 1] == '>')) return 1;

  int prev = i - 1;
  while (prev >= 0 && isspace((unsigned char)cmd[prev])) prev--;

  int next = i + 1;
  while (cmd[next] && isspace((unsigned char)cmd[next])) next++;

  if (prev >= 0 && next < (int)strlen(cmd)) {
    if (isalnum((unsigned char)cmd[prev]) || cmd[prev] == '_' || cmd[prev] == ')') {
      if (isdigit((unsigned char)cmd[next]) || cmd[next] == '\'' || cmd[next] == '"') {
        return 1;
      }
      if (i > 0 && !isspace((unsigned char)cmd[i - 1]) && !isspace((unsigned char)cmd[i + 1])) {
        return 1;
      }
    }
  }

  return 0;
}

/**
   @brief Parse redirection operators (<, >, >>) outside of quotes.
   @param cmd Command string to parse and modify in-place.
   @param infile Output pointer for input redirection file, or NULL.
   @param outfile Output pointer for output redirection file, or NULL.
   @param append Output int set to 1 for >> (append), 0 for > (truncate).
 */
int parse_redirection(char *cmd, char **infile, char **outfile, int *append)
{
  *infile = NULL;
  *outfile = NULL;
  *append = 0;

  char quote = 0;
  int len = strlen(cmd);

  for (int i = 0; i < len; i++) {
    if (!quote && (cmd[i] == '\'' || cmd[i] == '"')) {
      quote = cmd[i];
    } else if (quote && cmd[i] == quote) {
      quote = 0;
    } else if (!quote) {
      if (cmd[i] == '>' || cmd[i] == '<') {
        if (is_sql_comparison_operator(cmd, i)) {
          continue;
        }
      }

      if (cmd[i] == '>' && cmd[i + 1] == '>') {
        cmd[i] = '\0';
        cmd[i + 1] = '\0';
        i += 2;
        while (i < len && isspace((unsigned char)cmd[i])) i++;
        int start = i;
        while (i < len && !isspace((unsigned char)cmd[i]) && cmd[i] != '<') i++;
        int end = i;
        char saved = cmd[end];
        cmd[end] = '\0';
        *outfile = cmd + start;
        *append = 1;
        if (saved != '\0') { cmd[end] = saved; i = end - 1; } else { i = end; }
      } else if (cmd[i] == '>') {
        cmd[i] = '\0';
        i++;
        while (i < len && isspace((unsigned char)cmd[i])) i++;
        int start = i;
        while (i < len && !isspace((unsigned char)cmd[i]) && cmd[i] != '<') i++;
        int end = i;
        char saved = cmd[end];
        cmd[end] = '\0';
        *outfile = cmd + start;
        *append = 0;
        if (saved != '\0') { cmd[end] = saved; i = end - 1; } else { i = end; }
      } else if (cmd[i] == '<') {
        cmd[i] = '\0';
        i++;
        while (i < len && isspace((unsigned char)cmd[i])) i++;
        int start = i;
        while (i < len && !isspace((unsigned char)cmd[i]) && cmd[i] != '>') i++;
        int end = i;
        char saved = cmd[end];
        cmd[end] = '\0';
        *infile = cmd + start;
        if (saved != '\0') { cmd[end] = saved; i = end - 1; } else { i = end; }
      }
    }
  }

  trim_inplace(cmd);
  if (*infile) trim_inplace(*infile);
  if (*outfile) trim_inplace(*outfile);
  return 1;
}

/**
   @brief Split a command line by pipeline operators ('|') outside of quotes.
   @param line Full command line.
   @param commands Array to store pointers to command segments.
   @param max_cmds Maximum number of pipeline stages.
   @return Number of command segments found.
 */
int split_pipeline(char *line, char **commands, int max_cmds)
{
  int count = 0;
  char quote = 0;
  int len = strlen(line);
  int start = 0;

  for (int i = 0; i <= len; i++) {
    char c = line[i];
    if (!quote && (c == '\'' || c == '"')) {
      quote = c;
    } else if (quote && c == quote) {
      quote = 0;
    } else if (!quote && (c == '|' || c == '\0')) {
      line[i] = '\0';
      commands[count] = line + start;
      trim_inplace(commands[count]);
      count++;
      if (count >= max_cmds) break;
      start = i + 1;
    }
  }
  return count;
}

/**
   @brief Execute a single command stage with redirection support (<, >, >>).
   @param cmd_str The command string for this stage.
   @return 1 to continue execution, 0 to terminate shell.
 */
int dbsh_execute_single(char *cmd_str)
{
  char *infile = NULL;
  char *outfile = NULL;
  int append = 0;

  parse_redirection(cmd_str, &infile, &outfile, &append);

  int saved_stdin = -1;
  int saved_stdout = -1;

  if (infile != NULL && strlen(infile) > 0) {
    int fd_in = open(infile, O_RDONLY);
    if (fd_in < 0) {
      perror("dbsh: open input file");
      return 1;
    }
    saved_stdin = dup(STDIN_FILENO);
    dup2(fd_in, STDIN_FILENO);
    close(fd_in);
  }

  if (outfile != NULL && strlen(outfile) > 0) {
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd_out = open(outfile, flags, 0644);
    if (fd_out < 0) {
      perror("dbsh: open output file");
      if (saved_stdin != -1) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
      }
      return 1;
    }
    saved_stdout = dup(STDOUT_FILENO);
    dup2(fd_out, STDOUT_FILENO);
    close(fd_out);
  }

  // 1. Expandir alias definidos por el usuario
  char cmd_expanded[2048];
  char first_word[64] = "";
  sscanf(cmd_str, "%63s", first_word);
  const char *aliased = dbsh_find_alias(first_word);
  if (aliased != NULL) {
    char *rest = cmd_str + strlen(first_word);
    while (isspace((unsigned char)*rest)) rest++;
    if (strlen(rest) > 0) {
      snprintf(cmd_expanded, sizeof(cmd_expanded), "%s %s", aliased, rest);
    } else {
      snprintf(cmd_expanded, sizeof(cmd_expanded), "%s", aliased);
    }
  } else {
    strncpy(cmd_expanded, cmd_str, sizeof(cmd_expanded) - 1);
    cmd_expanded[sizeof(cmd_expanded) - 1] = '\0';
  }

  // 2. Expandir abreviaciones SQL inteligentes (ej: s * f mitabla w x>1)
  char *expanded_sql = dbsh_expand_shorthand(cmd_expanded);
  int status = 1;

  if (expanded_sql != NULL) {
    printf("[dbsh:sql] %s\n", expanded_sql);
    status = dbsh_execute_sql(expanded_sql);
    free(expanded_sql);
  } else {
    char *raw_cmd = strdup(cmd_expanded);
    char **args = dbsh_split_line(cmd_expanded);

    if (args[0] != NULL) {
      if (is_sql_command(args[0])) {
        const char *p = raw_cmd;
        while (isspace((unsigned char)*p)) p++;
        status = dbsh_execute_sql(p);
      } else {
        int is_builtin = 0;
        for (int i = 0; i < dbsh_num_builtins(); i++) {
          if (strcmp(args[0], builtin_str[i]) == 0) {
            status = (*builtin_func[i])(args);
            is_builtin = 1;
            break;
          }
        }
        if (!is_builtin) {
          status = dbsh_launch(args);
        }
      }
    }
    free(raw_cmd);
    free(args);
  }

  fflush(stdout);

  if (saved_stdin != -1) {
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
  }
  if (saved_stdout != -1) {
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
  }

  return status;
}

/**
   @brief Execute a pipeline of commands separated by '|'.
   @param line Full command line entered by user.
   @return 1 to continue execution, 0 to terminate shell.
 */
int dbsh_execute_pipeline(char *line)
{
  char *commands[32];
  int n = split_pipeline(line, commands, 32);

  if (n <= 0) {
    return 1;
  }

  if (n == 1) {
    return dbsh_execute_single(commands[0]);
  }

  int pipefds[2 * (n - 1)];
  for (int i = 0; i < n - 1; i++) {
    if (pipe(pipefds + i * 2) < 0) {
      perror("dbsh: pipe");
      return 1;
    }
  }

  for (int i = 0; i < n; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      if (i > 0) {
        dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
      }
      if (i < n - 1) {
        dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
      }

      for (int j = 0; j < 2 * (n - 1); j++) {
        close(pipefds[j]);
      }

      dbsh_execute_single(commands[i]);
      exit(EXIT_SUCCESS);
    } else if (pid < 0) {
      perror("dbsh: fork");
      return 1;
    }
  }

  for (int j = 0; j < 2 * (n - 1); j++) {
    close(pipefds[j]);
  }

  for (int i = 0; i < n; i++) {
    wait(NULL);
  }

  return 1;
}

/**
   @brief Read a line of input with arrow history navigation in TTY mode.
   @param prompt Prompt string to display.
   @return The line from stdin, or NULL on EOF.
 */
char *dbsh_read_line(const char *prompt)
{
  if (!isatty(STDIN_FILENO)) {
    char *line = NULL;
    size_t bufsize = 0;
    ssize_t nread = getline(&line, &bufsize, stdin);
    if (nread == -1) {
      free(line);
      return NULL;
    }
    while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
      line[--nread] = '\0';
    }
    if (strlen(line) > 0) {
      dbsh_add_history(line);
    }
    return line;
  }

  // Interactive terminal mode with termios
  struct termios orig_termios, raw;
  tcgetattr(STDIN_FILENO, &orig_termios);
  raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  char buf[2048] = "";
  int len = 0;
  int pos = 0;
  int hist_idx = dbsh_history_count;
  char *draft = NULL;

  printf("%s", prompt);
  fflush(stdout);

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '\r' || c == '\n') {
      printf("\n");
      break;
    } else if (c == 127 || c == '\b') {
      if (pos > 0) {
        memmove(&buf[pos - 1], &buf[pos], len - pos + 1);
        pos--;
        len--;
        printf("\r\033[K%s%s", prompt, buf);
        if (len > pos) {
          printf("\033[%dD", len - pos);
        }
        fflush(stdout);
      }
    } else if (c == 3) {
      // Ctrl+C
      printf("^C\n");
      buf[0] = '\0';
      len = 0;
      pos = 0;
      break;
    } else if (c == 4) {
      // Ctrl+D
      if (len == 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        free(draft);
        return NULL;
      }
    } else if (c == 27) {
      char seq[3];
      if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
        if (seq[0] == '[') {
          if (seq[1] == 'A') {
            // Flecha Arriba: anterior comando del historial
            if (dbsh_history_count > 0) {
              if (hist_idx == dbsh_history_count) {
                free(draft);
                draft = strdup(buf);
              }
              if (hist_idx > 0) {
                hist_idx--;
                strncpy(buf, dbsh_history_list[hist_idx], sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                len = strlen(buf);
                pos = len;
                printf("\r\033[K%s%s", prompt, buf);
                fflush(stdout);
              }
            }
          } else if (seq[1] == 'B') {
            // Flecha Abajo: siguiente comando del historial
            if (hist_idx < dbsh_history_count) {
              hist_idx++;
              if (hist_idx == dbsh_history_count) {
                if (draft) {
                  strncpy(buf, draft, sizeof(buf) - 1);
                  buf[sizeof(buf) - 1] = '\0';
                  free(draft);
                  draft = NULL;
                } else {
                  buf[0] = '\0';
                }
              } else {
                strncpy(buf, dbsh_history_list[hist_idx], sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
              }
              len = strlen(buf);
              pos = len;
              printf("\r\033[K%s%s", prompt, buf);
              fflush(stdout);
            }
          } else if (seq[1] == 'C') {
            // Flecha Derecha
            if (pos < len) {
              pos++;
              printf("\033[C");
              fflush(stdout);
            }
          } else if (seq[1] == 'D') {
            // Flecha Izquierda
            if (pos > 0) {
              pos--;
              printf("\033[D");
              fflush(stdout);
            }
          } else if (seq[1] == 'H') {
            // Inicio
            pos = 0;
            printf("\r\033[K%s%s", prompt, buf);
            if (len > 0) printf("\033[%dD", len);
            fflush(stdout);
          } else if (seq[1] == 'F') {
            // Fin
            pos = len;
            printf("\r\033[K%s%s", prompt, buf);
            fflush(stdout);
          } else if (seq[1] == '3') {
            // Tecla Delete: ESC [ 3 ~
            char tilda;
            if (read(STDIN_FILENO, &tilda, 1) == 1 && tilda == '~') {
              if (pos < len) {
                memmove(&buf[pos], &buf[pos + 1], len - pos);
                len--;
                printf("\r\033[K%s%s", prompt, buf);
                if (len > pos) {
                  printf("\033[%dD", len - pos);
                }
                fflush(stdout);
              }
            }
          }
        }
      }
    } else if (c == 1) {
      // Ctrl+A
      pos = 0;
      printf("\r\033[K%s%s", prompt, buf);
      if (len > 0) printf("\033[%dD", len);
      fflush(stdout);
    } else if (c == 5) {
      // Ctrl+E
      pos = len;
      printf("\r\033[K%s%s", prompt, buf);
      fflush(stdout);
    } else if (c == 21) {
      // Ctrl+U
      memmove(buf, &buf[pos], len - pos + 1);
      len -= pos;
      pos = 0;
      printf("\r\033[K%s%s", prompt, buf);
      if (len > 0) printf("\033[%dD", len);
      fflush(stdout);
    } else if (c == 11) {
      // Ctrl+K
      buf[pos] = '\0';
      len = pos;
      printf("\r\033[K%s%s", prompt, buf);
      fflush(stdout);
    } else if ((unsigned char)c >= 32 && (size_t)len < sizeof(buf) - 1) {
      memmove(&buf[pos + 1], &buf[pos], len - pos + 1);
      buf[pos] = c;
      pos++;
      len++;
      printf("\r\033[K%s%s", prompt, buf);
      if (len > pos) {
        printf("\033[%dD", len - pos);
      }
      fflush(stdout);
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
  free(draft);

  if (len > 0) {
    dbsh_add_history(buf);
  }

  return strdup(buf);
}

#define DBSH_TOK_BUFSIZE 64
#define DBSH_TOK_DELIM " \t\r\n\a"
/**
   @brief Split a line into tokens.
   @param line The line.
   @return Null-terminated array of tokens.
 */
char **dbsh_split_line(char *line)
{
  int bufsize = DBSH_TOK_BUFSIZE, position = 0;
  char **tokens = malloc(bufsize * sizeof(char*));
  char *token, **tokens_backup;

  if (!tokens) {
    fprintf(stderr, "dbsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  token = strtok(line, DBSH_TOK_DELIM);
  while (token != NULL) {
    tokens[position] = token;
    position++;

    if (position >= bufsize) {
      bufsize += DBSH_TOK_BUFSIZE;
      tokens_backup = tokens;
      tokens = realloc(tokens, bufsize * sizeof(char*));
      if (!tokens) {
		free(tokens_backup);
        fprintf(stderr, "dbsh: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }

    token = strtok(NULL, DBSH_TOK_DELIM);
  }
  tokens[position] = NULL;
  return tokens;
}

/**
   @brief Loop getting input and executing it.
 */
void dbsh_loop(void)
{
  char *line;
  int status = 1;

  do {
    line = dbsh_read_line("dbsh> ");
    if (line == NULL) {
      printf("\n");
      break;
    }
    status = dbsh_execute_pipeline(line);
    free(line);
  } while (status);
}

/**
   @brief Main entry point.
   @param argc Argument count.
   @param argv Argument vector.
   @return status code
 */
int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  // Cargar historial previo de ~/.dbsh_history
  dbsh_load_history();

  // Load config files, if any.
  dbsh_load_config();

  if (config.auto_connect) {
    dbsh_auto_connect();
  }

  // Run command loop.
  dbsh_loop();

  // Guardar historial en ~/.dbsh_history
  dbsh_save_history();

  // Perform any shutdown/cleanup.
  if (db_conn != NULL) {
    PQfinish(db_conn);
    db_conn = NULL;
  }

  return EXIT_SUCCESS;
}


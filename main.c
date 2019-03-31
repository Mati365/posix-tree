#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h> // PATH_MAX
#include <unistd.h>

#define INITIAL_STACK_SIZE 64
#define STACK_CHUNK_SIZE 64

/**
 * Implementacja stosu
 */
typedef struct v_stack {
  void** data;
  unsigned int size; // ilość zaalokowanych int'ów
  unsigned int count; // ilość elementów na stosie
} v_stack;

v_stack v_stack_init() {
  return (v_stack) {
    .data = malloc(INITIAL_STACK_SIZE * sizeof(void*)),
    .size = INITIAL_STACK_SIZE,
    .count  = 0,
  };
}

void v_stack_push(v_stack* stk, void* data) {
  // rozszerz stos
  if (stk->count + 1 >= stk->size) {
    stk->size += STACK_CHUNK_SIZE;

    void* new_buffer = malloc(stk->size * sizeof(void*));
    memcpy(new_buffer, stk->data, (stk->size - STACK_CHUNK_SIZE) * sizeof(void*));
    free(stk->data);
    stk->data = new_buffer;
  }

  // przypisz dane
  stk->data[stk->count] = data;

  // zwiększ ilość elementów na stosie
  stk->count++;
}

void* v_stack_peek(v_stack* stk) {
  if (stk->count == 0)
    return NULL;

  return stk->data[stk->count - 1];
}

int v_stack_pop(v_stack* stk, bool release) {
  // todo: Dodac realloc?
  if (stk->count > 0) {
    if (release)
      free(stk->data[stk->count - 1]);

    stk->count--;
    return 0;
  }

  return -1;
}

int v_stack_release(v_stack* stk, bool release_nested) {
  if (stk->data == NULL)
    return -1;

  // kasuje nested itemki void*
  if (release_nested) {
    for (int i = stk->count - 1; i >= 0; --i)
      free(stk->data[i]);
  }

  free(stk->data);
  stk->data = NULL;

  return 0;
}

/**
 * Implementacja tree
 */
#define MAX_POSIX_FILENAME_LEN 256
#define TREE_LEVEL_SPACING_LEN 3
#define TREE_EDGE_CHARACTER "├"
#define TREE_LINE_CHARACTER "─"
#define TREE_ROW_CHARACTER "│"
#define TREE_EDGE_TERMINATOR_CHARACTER "└"

typedef struct tree_file {
  unsigned char type; // DT_DIR, DT_REG
  bool is_dir;
  bool is_symlink;
  char* name;
  char* path;

  // daty dostępu
  time_t last_access_time;
  time_t last_modificiation_time;

  off_t size; // w bajtach rozmiar

  // jeśli symlink
  struct tree_file* symlink_dest;
} tree_file;

// informacje podsumowujące drzewo
typedef struct tree_stats {
  unsigned int files_count;
  unsigned int dir_count;
} tree_stats;

// informacje używane do rysowania drzewa
typedef struct tree_print_flags {
  bool dir_only;
  bool follow_symlinks;
  bool print_full_path;
  int max_level;
} tree_print_flags;

/**
 * Łączy dwie ścieżki
 */
char* join_paths(const char* parent, const char* child) {
  if (!parent || !child)
    return NULL;

  // jeśli druga część ścieżki zaczyna się od /
  // lub rodzic kończy się na / to łącz bez slasha
  char* output = NULL;
  int parent_length = strlen(parent);
  int total_length = parent_length + strlen(child);

  if (child[0] == '/' || parent[parent_length - 1] == '/') {
    output = malloc(total_length);
    sprintf(output, "%s%s", parent, child);
  } else {
    output = malloc(total_length + 1);
    sprintf(output, "%s/%s", parent, child);
  }

  return output;
}

/**
 * Listuje pliki w katalogu wskazanym w path.
 * Zwraca stos ze wskaźnikami do tree_file
 */
v_stack tree_list_files(
    const char* path,
    struct tree_print_flags* flags) {
  DIR* dp = opendir(path);
  struct dirent *ep;
  struct v_stack files = v_stack_init();

  if (dp == NULL)
    return files;

  // wrzucanie info o pliku do struktury
  while ((ep = readdir(dp))) {
    // ignorowanie . i ..
    if (!strcmp("..", ep->d_name) || !strcmp(".", ep->d_name))
      continue;

    // ignorowanie plików jeśli tylko foldery przyzwolone są
    bool is_dir = ep->d_type == DT_DIR;
    if (!is_dir && flags->dir_only)
      continue;

    struct tree_file* file = malloc(sizeof(tree_file));

    // Zbieranie informacji z pliku
    file->name = malloc(MAX_POSIX_FILENAME_LEN);
    memcpy(file->name, ep->d_name, MAX_POSIX_FILENAME_LEN);

    file->symlink_dest = NULL;
    file->path = join_paths(path, file->name);
    file->type = ep->d_type;
    file->is_dir = is_dir;
    file->is_symlink = ep->d_type == DT_LNK;

    // zbieranie bardziej zaawansowanych informacji o pliku
    struct stat file_stat;
    stat(file->path, &file_stat);

    file->size = file_stat.st_size;
    file->last_modificiation_time = file_stat.st_mtime;
    file->last_access_time = file_stat.st_atime;

    // pobieranie informacji nt. symlinków
    if (file->is_symlink) {
      char* relative_path = malloc(MAX_POSIX_FILENAME_LEN + 1);
      size_t buf = readlink(file->path, relative_path, MAX_POSIX_FILENAME_LEN);
      relative_path[MAX_POSIX_FILENAME_LEN] = '\0';

      if (buf) {
        struct stat link_stat;
        struct tree_file* symlink_dest = malloc(sizeof(tree_file));

        // nazwa wyswietlana zlimitowana jest
        symlink_dest->name = relative_path;

        // absolutna ścieżka do symlinka
        // ścieżka relatywna ./test prefixowana jest jeszcze z nazwą pliku
        // czyli ./nazwa_pliku/./test i z tego liczona jest absolutna ściezka
        char* fixed_relative_path = join_paths(path, relative_path);
        char* absolute_path = malloc(PATH_MAX);
        realpath(fixed_relative_path, absolute_path);
        symlink_dest->path = absolute_path;
        free(fixed_relative_path);

        // żeby podczas release się nie zawiesiło
        stat(absolute_path, &link_stat);

        symlink_dest->is_dir = S_ISDIR(link_stat.st_mode);
        symlink_dest->symlink_dest = NULL;

        // przypisanie pliku
        file->symlink_dest = symlink_dest;

      } else {
        // jeśli coś poszło nie tak :(
        free(relative_path);
      }
    }

    // wrzucenie itemka na stos
    v_stack_push(&files, (void*) file);
  }

  closedir(dp);
  return files;
}

/**
 * Usuwanie stosu z plikami, ścieżki są dynamicznie alokowane
 */
void tree_free_file(tree_file* file) {
  free(file->path);
  free(file->name);

  if (file->symlink_dest != NULL)
    tree_free_file(file->symlink_dest);

  free(file);
}

void tree_release_files(v_stack* stk) {
  for (int i = 0; i < stk->count; ++i) {
    struct tree_file* file = (tree_file*)stk->data[i];
    tree_free_file(file);
  }

  v_stack_release(stk, false);
}

/**
 * Rysuje drzewo we wskazanej ścieżce
 */
int tree_recursive_print(
    const char* path,
    struct v_stack* parents, // przechowuje flagi czy parent jest ostatnim elementem
    struct tree_print_flags* flags,
    struct tree_stats* stats) {

  int level = parents->count;
  if (level == flags->max_level)
    return -1;

  // lista zawierająca listę rodziców przetwarzanego wierzchołka
  struct v_stack stk = tree_list_files(path, flags);

  // aktualny katalog
  if (!level)
    printf("%s\n", path);

  for (int i = 0; i < stk.count; ++i) {
    const tree_file* file = (tree_file*) stk.data[i];
    bool last_file = i == stk.count - 1;

    // wyświetlanie "|" na poczatku linii levelu
    for (int i = 0; i < level; ++i) {
      bool is_last_parent = (bool) parents->data[i];

      printf(
          "%s%*s",
          // rozpoczęcie linii, jeśli level jest ostatni
          // to nie wyświetlaj separatora
          (is_last_parent ? " " : TREE_ROW_CHARACTER),
          // %*s powtarza spację
          TREE_LEVEL_SPACING_LEN, "");
    }

    // wyświetlanie "|--- " do pliku
    printf(
        "%s%s ",
        // znak rozpoczynający linię
        last_file
          ? TREE_EDGE_TERMINATOR_CHARACTER
          : TREE_EDGE_CHARACTER,

        // znak linii
        TREE_LINE_CHARACTER""TREE_LINE_CHARACTER);

    // wyświetlenie i formatowanie nazwy pliku
    // todo: Symlinki i formatowanie nazwy
    const char* file_title = file->name;

    // wyświetlanie całych ścieżek
    if (flags->print_full_path)
      file_title = file->path;

    // wyświetlanie symlinka
    if (file->is_symlink && file->symlink_dest) {
      const tree_file* dest_file = file->symlink_dest;

      printf("\33[1;31m%s\33[m -> ", file->path);
      if (dest_file->is_dir) {
        printf("\33[1;34m%s\33[m", dest_file->name);
        if (flags->follow_symlinks)
          printf(" [follow]");
        printf("\n");
      } else
        printf("%s\n", dest_file->name);

    } else if (file->is_dir)
      printf("\33[1;34m%s\33[m\n", file->path);
    else
      printf("%s\n", file->path);

    // jeśli to ścieżka to rekursywnie pokaż następny level
    // todo: Follow symlinks
    if (file->is_dir) {
      stats->dir_count++;

      // wrzuca na stos info czy parent jest ostatni
      v_stack_push(parents, (void*) last_file);
      tree_recursive_print(
          file->path,
          parents,
          flags,
          stats);
      v_stack_pop(parents, false);
    } else {
      stats->files_count++;

      // jeśli symlink i pozwala na follow
      if (file->symlink_dest && file->symlink_dest->is_dir && flags->follow_symlinks) {
        v_stack_push(parents, (void*) last_file);
        tree_recursive_print(
            file->symlink_dest->path,
            parents,
            flags,
            stats);
        v_stack_pop(parents, false);
      }
    }
  }

  tree_release_files(&stk);
  return 0;
}

/**
 * Główna funkcja wyświetlająca i podsumowująca drzewo
 */
void tree_print(
    const char* path,
    struct tree_print_flags* flags) {
  struct tree_stats stats = {
    .files_count = 0,
    .dir_count = 0,
  };

  // rendering drzewa
  struct v_stack parents = v_stack_init();
  tree_recursive_print(path, &parents, flags, &stats);
  v_stack_release(&parents, false);

  // wyświetlanie ilości folderów
  printf("\n");
  if (stats.dir_count == 1)
    printf("1 directory, ");
  else
    printf("%d directories, ", stats.dir_count);

  // wyświetlanie ilości plików
  if (stats.files_count == 1)
    printf("1 file\n");
  else
    printf("%d files\n", stats.files_count);
}

/**
 * Opcje:
 * -d Dir onlly
 * -L Max level
 * -l Follow symlinks
 * -f Print full path
 */
int main() {
  struct tree_print_flags flags = {
    .print_full_path = true,
    .dir_only = false,
    .follow_symlinks = true,
    .max_level = 4,
  };

  tree_print(".", &flags);
  return 0;
}

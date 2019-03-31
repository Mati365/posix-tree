# posix-tree
Simple header only lib that allows to print folder content in UNIX tree like form.

### Example
```c
  struct tree_print_flags flags = {
    .print_full_path = true,
    .dir_only = false,
    .follow_symlinks = true,
    .max_level = 3,
  };

  tree_print(".", &flags);
```
output: <br />
![Screen](/screens/1.png?raw=true "Screen")


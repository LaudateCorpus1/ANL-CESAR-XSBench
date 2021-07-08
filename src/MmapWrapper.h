void *mmap_wrapper( char *grid_type, char *data_name, size_t length);

int munmap_wrapper(char *grid_type, char *data_name, void *memPtr );

int munmap_wrapper_cleanup();

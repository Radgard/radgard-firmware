
void storage_init_nvs();

esp_err_t storage_set(char *key, char *value);

esp_err_t storage_get(char *key, char *value, size_t *size);

esp_err_t storage_size(char *key, size_t *size);
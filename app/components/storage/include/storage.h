
const char *STORAGE_USER_ID;

void storage_init_nvs();

esp_err_t storage_set(const char *key, const char *value);

esp_err_t storage_get(const char *key, char *value, size_t *size);

esp_err_t storage_size(const char *key, size_t *size);
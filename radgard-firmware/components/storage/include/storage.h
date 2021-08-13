
const char *STORAGE_VERSION;

const char *STORAGE_USER_ID;
const char *STORAGE_ZONE_ID;

const char *STORAGE_TIME_ZONE;
const char *STORAGE_TIMES_LENGTH;
const char *STORAGE_TIMES_BASE;
const char *STORAGE_TIME_INDEX;

void storage_init_nvs();
void storage_deinit_nvs();

esp_err_t storage_set_str(const char *key, const char *value);

esp_err_t storage_set_u8(const char *key, uint8_t value);

esp_err_t storage_set_u32(const char *key, uint32_t value);

esp_err_t storage_get_str(const char *key, char *value, size_t *size);

esp_err_t storage_get_u8(const char *key, uint8_t *value);

esp_err_t storage_get_u32(const char *key, uint32_t *value);

esp_err_t storage_get_str_size(const char *key, size_t *size);

void storage_reset();
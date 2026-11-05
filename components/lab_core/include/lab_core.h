#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB_FW "1.0.0"
#define LAB_G0 9.80665f
/* Engineering calibration criteria, SI in S; not a metrological guarantee.
 * Build-time configuration. Keep source-record statistics for independent checks. */
#define LAB_CAL_GYRO_MOTION_RAD_S 0.35f
#define LAB_CAL_ACCEL_NORM_TOL_G 0.15f
#define LAB_CAL_ACCEL_STD_M_S2 0.1f
#define LAB_CAL_GYRO_STD_RAD_S 0.015f
#define LAB_CAL_FACE_PRIMARY_G 0.85f
#define LAB_CAL_FACE_CROSS_G 0.25f
#define LAB_CAL_SOLVE_PRIMARY_G 0.75f
#define LAB_CAL_SOLVE_CROSS_G 0.3f
#define LAB_CAL_GYRO_BIAS_MAX_RAD_S 0.5f
#define LAB_CAL_ACCEL_BIAS_MAX_M_S2 5.0f
#define LAB_CAL_SCALE_MIN 0.5f
#define LAB_CAL_SCALE_MAX 1.5f
#define LAB_RING_CAP 832
#define LAB_BATCH 8
#define LAB_JSON_MAX 6144
#define LAB_PACKET_MAX (LAB_JSON_MAX + 40)
#define LAB_HEADER 36
#define LAB_RECORD 46
#define LAB_ORIENT_INVALID 1u
#define LAB_ACCEL_INVALID 2u
#define LAB_ACCEL_NORM 4u
#define LAB_SATURATED 8u
#define LAB_TIME_GAP 16u
#define LAB_UNCALIBRATED 32u
#define LAB_CALIBRATION 64u
#define LAB_REPLAY 1u /* delivery, never a sample flag */

typedef struct {
    int rate, accel_range, gyro_range, dlpf;
    float kp, ki, g0;
    int8_t r_bs[9];
    bool use_bias;
} lab_config;
typedef struct {
    uint32_t version;
    uint64_t uid;
    float gyro_bias[3], accel_bias[3], accel_scale[3];
    float temperature, gyro_std[3], accel_std[6][3];
    float face_mean[6][3];
    uint32_t gyro_n, face_n[6];
    int accel_range, gyro_range;
    char method[24];
    bool valid;
} lab_cal;
typedef struct { uint32_t version,size; lab_cal cal; uint32_t crc; } lab_saved_cal;
typedef struct { char client[64],id[64],reply[512]; uint32_t hash; } lab_cached_request;
typedef struct { lab_cached_request entries[16]; uint32_t next; } lab_request_cache;
/* Lookup: 0 absent, 1 same bytes/reply, -1 reused id with different bytes. */
int lab_request_lookup(lab_request_cache *cache,const char *client,const char *id,uint32_t hash,const char **reply);
bool lab_request_store(lab_request_cache *cache,const char *client,const char *id,uint32_t hash,const char *reply);
/* 0 allowed, 1 experiment INVALID_STATE, 2 calibration BUSY. */
int lab_mutation_policy(bool experiment,int wizard_state,const char *command);
bool lab_saved_valid(const lab_saved_cal *blob,size_t length,uint64_t uid);
typedef struct {
    float q[4], integral[3];
    int64_t prev_us;
    bool ready;
    uint32_t still_n;
    float still_mean[3], still_m2[3];
} lab_attitude;
typedef struct {
    uint32_t seq, flags;
    int64_t t_us;
    int16_t raw[7]; /* ax ay az gx gy gz temperature */
    float q[4]; /* state AFTER this sample */
} lab_sample;
typedef struct {
    lab_sample data[LAB_RING_CAP];
    uint32_t next, count;
} lab_ring;
typedef struct {
    /* state: 0 idle, 1 gyro, 2 waiting face, 3 collecting face, 4 candidate, -1 rejected */
    int state, face;
    uint8_t faces;
    uint32_t n, target;
    int64_t first_us, last_us;
    float mean[7], m2[7];
    lab_cal candidate;
    char reason[64];
} lab_wizard;

void lab_defaults(lab_config *c, lab_cal *cal, uint64_t uid);
bool lab_config_valid(const lab_config *c);
bool lab_cal_valid(const lab_cal *c, uint64_t uid, bool transfer);
void lab_nominal(const lab_config *c, const int16_t raw[7], float a[3], float w[3], float *temp);
void lab_correct(const lab_config *c, const lab_cal *cal, const int16_t raw[7], float a[3], float w[3], float *temp);
void lab_att_reset(lab_attitude *s);
bool lab_mahony(lab_attitude *s, const float a[3], const float w[3], float kp, float ki, float dt);
uint32_t lab_att_step(lab_attitude *s, const lab_config *c, const float a[3], const float w[3], int64_t t_us);
void lab_euler(const float q[4], float rpy[3]);
void lab_zero_yaw(lab_attitude *s);
void lab_ring_clear(lab_ring *r);
bool lab_ring_put(lab_ring *r, lab_sample *s);
size_t lab_ring_get(const lab_ring *r, uint32_t *cursor, lab_sample *out, size_t cap, bool *gap);
void lab_wizard_start(lab_wizard *w, const lab_config *c, const lab_cal *cal, bool gyro, unsigned seconds);
bool lab_wizard_face(lab_wizard *w, int face, int rate, unsigned seconds);
void lab_wizard_feed(lab_wizard *w, const lab_config *c, const lab_sample *s);
bool lab_six_solve(lab_cal *cal, float g0);

uint32_t lab_crc32(const uint8_t *data, size_t n);
size_t lab_cobs_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
size_t lab_cobs_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
void lab_put16(uint8_t *p, uint16_t v);
void lab_put32(uint8_t *p, uint32_t v);
void lab_put64(uint8_t *p, uint64_t v);
uint16_t lab_get16(const uint8_t *p);
uint32_t lab_get32(const uint8_t *p);
uint64_t lab_get64(const uint8_t *p);
size_t lab_packet(uint8_t *out, size_t cap, uint8_t type, uint16_t delivery,
                  uint64_t uid, uint64_t boot, uint64_t stream, uint32_t config,
                  const uint8_t *payload, size_t n);
bool lab_packet_valid(const uint8_t *p, size_t n);
size_t lab_samples_payload(uint8_t *out, const lab_sample *s, size_t n);

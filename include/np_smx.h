#ifndef NP_SMX_H
#define NP_SMX_H

#include "np_types.h"
#include <stdint.h>

#define NP_SMX_SEC 32 /* seconds kept */
#define NP_CUBE_BUDGET 40
#define NP_CUBE_CR 242
#define NP_CUBE_CG 38
#define NP_CUBE_CB 71
#define NP_CUBE3 8
#define NP_CUBE3_N 512
#define NP_VIRT_MAX 16
#define NP_VIRT_NAME 12

/* One bit per used channel, one row per second + fixed 8^3 cube. */
struct np_virt {
    char name[NP_VIRT_NAME];
    uint8_t x, y, z, used;
};

struct np_smx {
    uint8_t bit[NP_SMX_SEC][NP_NCHAN];
    uint8_t nch;
    uint8_t mask;
    uint32_t wr;
    uint32_t have;
    uint32_t seq;
    uint8_t cube[NP_CUBE3_N];
    uint8_t kind[NP_CUBE3_N];
    struct np_virt virt[NP_VIRT_MAX];
};

/* cube.viz_frame.v1 cell — role 0 lattice 1 core 2 channel */
struct np_cube {
    float x, y, z, s;
    uint8_t r, g, b, a;
    uint8_t role;
};

/* Scalp site. az 0 = nose, + = right (deg). el 0 = ear line, + = vertex. */
#define NP_ELEC_NAME 6
#define NP_1010_N 61 /* 10-10 nodes on the kit headset (no Nz/Iz/A1/A2) */
struct np_elec {
    float az, el;
    int site; /* 10-10 index, or -1 */
    char name[NP_ELEC_NAME];
};

#define NP_HEAD_R 1.15f
#define NP_PAIR_N 4 /* FCz-CPz, CP4-FC3, FC4-CP3, C3-C4 */

void np_smx_init(struct np_smx *m);
void np_smx_push(struct np_smx *m, const uint8_t bits[NP_NCHAN], int nch, uint8_t mask);
/* Newest-first 0/1 string, length have*nch. */
int np_smx_pack(const struct np_smx *m, char *out, int cap);
/* Used-channel ids 1..8 in matrix column order. */
int np_smx_ch_ids(const struct np_smx *m, int ids[NP_NCHAN]);
/* Latest second, remapped to ch0..ch7 bits. Packed slots are not channel ids. */
unsigned int np_smx_fold_ch(const struct np_smx *m);
/* Crimson BrainCube cells, budget ≤ 40 (core + last seconds). */
int np_smx_cubes(const struct np_smx *m, struct np_cube *out, int cap);

void np_elec_default(struct np_elec e[NP_NCHAN]);
int np_pair_count(void);
const char *np_pair_site_a(int pair);
const char *np_pair_site_b(int pair);
/* Channel indices for a named pair, or -1 if a site is unmapped. */
int np_pair_chs(const struct np_elec e[NP_NCHAN], int pair, int *cha, int *chb);
void np_elec_set_site(struct np_elec *e, int site);
void np_elec_to_xyz(const struct np_elec *e, float r, float *x, float *y, float *z);
void np_elec_from_xyz(float x, float y, float z, struct np_elec *e);
/* Fixed cell on the BrainCube. Same site → same xyz every frame. */
void np_elec_cube_xyz(const struct np_elec *e, float *x, float *y, float *z);
void np_1010_cube_xyz(int site, float *x, float *y, float *z);
int np_1010_count(void);
const char *np_1010_name(int i);
int np_1010_core(int i); /* 1 = 10-20 name printed larger on the headset */
void np_1010_elaz(int i, float *az, float *el);
void np_1010_flat(int i, float *fx, float *fy); /* +x right, +y nose, unit cap */
int np_1010_find(const char *name);
int np_1010_nearest(float az, float el);
/* How many 10-10 names share this shell cell. Writes up to cap site ids. */
int np_1010_sites_at(int x, int y, int z, int out[], int cap);
void np_view_apply(float yaw, float pitch, float x, float y, float z, float *ox, float *oy,
                   float *oz);
void np_view_undo(float yaw, float pitch, float x, float y, float z, float *ox, float *oy,
                  float *oz);
/* Core + lattice + channel cells. rgb may be NULL (crimson). */
int np_smx_head_cubes(const struct np_smx *m, const struct np_elec e[NP_NCHAN],
                      const int rgb[NP_NCHAN][3], struct np_cube *out, int cap);

#endif

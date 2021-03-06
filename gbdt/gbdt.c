/* ========================================================
 *   Copyright (C) 2016 All rights reserved.
 *   
 *   filename : gbdt.c
 *   author   : liuzhiqiangruc@126.com
 *   date     : 2016-02-26
 *   info     : implementation for gbdt
 * ======================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "gbdt.h"

struct _gbdt {
    DTD * train_ds;
    DTD *  test_ds;
    double * f;
    double * t;
    int tree_size;
    DTree ** dts;
    GBMP p;
    G g_fn;
    H h_fn;
    R r_fn;
};

static void load_init(GBDT * gbdt){
    int i;
    FILE * fp = NULL;
    if (gbdt->train_ds && gbdt->p.train_init){
        if (NULL == (fp = fopen(gbdt->p.train_init, "r"))){
            return;
        }
        for (i = 0; i < gbdt->train_ds->row; i++){
            fscanf(fp, "%lf", gbdt->f + i);
        }
        fclose(fp);
    }
    if (gbdt->test_ds && gbdt->p.test_init){
        if (NULL == (fp = fopen(gbdt->p.test_init, "r"))){
            return;
        }
        for (i = 0; i < gbdt->test_ds->row; i++){
            fscanf(fp, "%lf", gbdt->t + i);
        }
        fclose(fp);
    }
}

static void eval_test(GBDT * gbdt){
    int i, l = gbdt->test_ds->row;
    double *t = (double *)malloc(sizeof(double) * l);
    memset(t, 0, sizeof(double) * l);
    eval_tree(gbdt->test_ds, gbdt->dts[gbdt->tree_size - 1], t, l);
    for (i = 0; i < l; i++){
        gbdt->t[i] += t[i] * gbdt->p.rate;
    }
    free(t);
    t = NULL;
}

static void gbdt_save_score (GBDT * gbdt){
    int i;
    FILE * fp = NULL;
    char outfile[200] = {0};
    snprintf(outfile, 200, "%s/train_score.scr", gbdt->p.out_dir);
    if(NULL == (fp = fopen(outfile, "w"))){
        return;
    }
    for (i = 0; i < gbdt->train_ds->row; i++){
        fprintf(fp, "%.10f\n", gbdt->f[i]);
    }
    fclose(fp);
    if (gbdt->test_ds){
        snprintf(outfile, 200, "%s/test_score.scr", gbdt->p.out_dir);
        if (NULL == (fp = fopen(outfile, "w"))){
            return;
        }
        for (i = 0; i < gbdt->test_ds->row; i++){
            fprintf(fp, "%.10f\n", gbdt->t[i]);
        }
        fclose(fp);
    }
}

GBDT * gbdt_create(G g_fn, H h_fn, R r_fn, GBMP p){
    GBDT * gbdt = (GBDT*)malloc(sizeof(GBDT));
    if (!gbdt){
        goto gb_failed;
    }
    memset(gbdt, 0, sizeof(GBDT));
    gbdt->g_fn = g_fn;
    gbdt->h_fn = h_fn;
    gbdt->r_fn = r_fn;
    gbdt->p = p;
    gbdt->dts = (DTree **)malloc(sizeof(void *) * p.max_trees);
    if (!gbdt->dts){
        goto dts_failed;
    }
    memset(gbdt->dts, 0, sizeof(void *) * p.max_trees);
    DTD *(*tds)[2] = load_data(p.train_input, p.test_input, p.binary);
    if (!tds){
        goto ds_failed;
    }
    gbdt->train_ds = (*tds)[0];
    gbdt->f = (double*)malloc(sizeof(double) * gbdt->train_ds->row);
    if (!gbdt->f){
        goto train_y_failed;
    }
    memset(gbdt->f, 0, sizeof(double) * gbdt->train_ds->row);
    gbdt->test_ds  = (*tds)[1];
    if (!gbdt->test_ds){
        goto ret_no_test;
    }
    gbdt->t = (double*)malloc(sizeof(double) * gbdt->test_ds->row);
    if (!gbdt->t){
        goto test_y_failed;
    }
    memset(gbdt->t, 0, sizeof(double) * gbdt->test_ds->row);
ret_no_test:
    load_init(gbdt);
    return gbdt;
test_y_failed:
    free_data(gbdt->test_ds);
    gbdt->test_ds = NULL;
    free(gbdt->f);
    gbdt->f = NULL;
train_y_failed:
    free_data(gbdt->train_ds);
    gbdt->train_ds = NULL;
ds_failed:
    free(gbdt->dts);
    gbdt->dts = NULL;
dts_failed:
    free(gbdt);
    gbdt = NULL;
gb_failed:
    return NULL;
}

int    gbdt_train(GBDT * gbdt){
    int i, j, m, n = gbdt->train_ds->row;
    double * f = (double*)malloc(sizeof(double) * n);
    double * g = (double*)malloc(sizeof(double) * n);
    double * h = (double*)malloc(sizeof(double) * n);
    memset(f, 0, sizeof(double) * n);
    memset(g, 0, sizeof(double) * n);
    memset(h, 0, sizeof(double) * n);
    gbdt->tree_size = 0;
    m = gbdt->p.min_node_ins;
    if (m < 1){
        m = (int)(0.5 * n / gbdt->p.max_leaf_nodes);
        if (m < 1){
            m = 1;
        }
    }
    mkdir(gbdt->p.out_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    for (i = 0; i < gbdt->p.max_trees; i++) {
        gbdt->g_fn(gbdt->f, gbdt->train_ds->y, g, n, &gbdt->p);
        gbdt->h_fn(gbdt->f, gbdt->train_ds->y, h, n, &gbdt->p);
        DTree * tt = generate_dtree(gbdt->train_ds, f, g, h, gbdt->p.nod_reg, gbdt->p.wei_reg, n, gbdt->p.pnc, m, gbdt->p.max_depth, gbdt->p.max_leaf_nodes);
        if (tt){
#ifdef DTREE_DEBUG
            char subdir[200] = {0};
            char outfile[200] = {0};
            snprintf(subdir, 200, "%s/dtrees", gbdt->p.out_dir);
            mkdir(subdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            snprintf(outfile, 200, "%s/%d.dat", subdir, i);
            save_dtree(tt, outfile, gbdt->train_ds->id_map);
#endif
            gbdt->dts[i] = tt;
            gbdt->tree_size += 1;
            for (j = 0; j < n; j++){
                gbdt->f[j] += f[j] * gbdt->p.rate;
            }
            memset(f, 0, sizeof(double) * n);
            if (gbdt->test_ds){
               eval_test(gbdt); 
            }
            gbdt->r_fn(gbdt);
        }
        else{
            break;
        }
    }
    free(f);  f = NULL;
    free(g);  g = NULL;
    free(h);  h = NULL;
    return 0;
}

void gbdt_save(GBDT * gbdt){
    int i, n, s = size_dtree(gbdt->dts[0]);
    char outfile[200] = {0};
    FILE  * fp = NULL;
    DTree * st = NULL;
    snprintf(outfile, 200, "%s/gbdtree.mdl", gbdt->p.out_dir);
    if (NULL == (fp = fopen(outfile, "wb"))){
        return ;
    }
    fwrite(&gbdt->train_ds->col, sizeof(int), 1, fp);
    fwrite(gbdt->train_ds->id_map, sizeof(char[FKL]), gbdt->train_ds->col, fp);
    st = (DTree*)calloc(2000, s);
    for (i = 0; i < gbdt->tree_size; i++){
        n = serialize_dtree(gbdt->dts[i], st);
        fwrite(&n, sizeof(int), 1, fp);
        fwrite(st, s, n, fp);
    }
    fclose(fp);
    free(st); st = NULL;
    gbdt_save_score(gbdt);
    return;
}

void   gbdt_free (GBDT * gbdt){
    int i;
    if (gbdt){
        if (gbdt->train_ds){
            free_data(gbdt->train_ds);
            gbdt->train_ds = NULL;
        }
        if (gbdt->test_ds){
            free(gbdt->test_ds);
            gbdt->test_ds = NULL;
        }
        if (gbdt->f){
            free(gbdt->f);
            gbdt->f = NULL;
        }
        if (gbdt->t){
            free(gbdt->t);
            gbdt->t = NULL;
        }
        if (gbdt->dts){
            for (i = 0; i < gbdt->tree_size; i++){
                if (gbdt->dts[i]){
                    free_dtree(gbdt->dts[i]);
                    gbdt->dts[i] = NULL;
                }
            }
            free(gbdt->dts);
            gbdt->dts = NULL;
        }
        free(gbdt);
    }
}

int y_rowns(GBDT * gbdt){
    return gbdt->train_ds->row;
}

int t_rowns(GBDT * gbdt){
    return gbdt->test_ds->row;
}

int y_colns(GBDT * gbdt){
    return gbdt->train_ds->col;
}

int t_colns(GBDT * gbdt){
    return gbdt->test_ds->col;
}

double * y_model(GBDT * gbdt){
    return gbdt->f;
}

double * t_model(GBDT * gbdt){
    return gbdt->t;
}

double * y_label(GBDT * gbdt){
    return gbdt->train_ds->y;
}

double * t_label(GBDT * gbdt){
    return gbdt->test_ds->y;
}

int t_size(GBDT * gbdt){
    return gbdt->tree_size;
}

int has_test(GBDT * gbdt){
    return (gbdt->test_ds ? 1 : 0);
}

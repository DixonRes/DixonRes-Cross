#include "rational_system_solver.h"
#include <ctype.h>
#include <stdarg.h>

static int rational_solver_realtime_progress_enabled = 0;

void rational_solver_set_realtime_progress(int enabled) {
    rational_solver_realtime_progress_enabled = enabled;
}

static void rational_solver_progress(const char *fmt, ...) {
    if (!rational_solver_realtime_progress_enabled) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Progress: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
}

static char *rational_join_variable_names(rational_variable_info_t *vars, slong num_vars) {
    if (!vars || num_vars <= 0) {
        return NULL;
    }

    size_t total_len = 1;
    for (slong i = 0; i < num_vars; i++) {
        total_len += strlen(vars[i].name) + 2;
    }

    char *result = (char *) malloc(total_len);
    if (!result) {
        return NULL;
    }

    result[0] = '\0';
    for (slong i = 0; i < num_vars; i++) {
        if (i > 0) {
            strcat(result, ", ");
        }
        strcat(result, vars[i].name);
    }

    return result;
}

static char *rational_format_equation_index_list(const slong *indices, slong count) {
    if (!indices || count <= 0) {
        return NULL;
    }

    size_t total_len = 1;
    for (slong i = 0; i < count; i++) {
        total_len += 24;
    }

    char *result = (char *) malloc(total_len);
    if (!result) {
        return NULL;
    }

    result[0] = '\0';
    for (slong i = 0; i < count; i++) {
        char piece[32];
        if (i > 0) {
            strcat(result, ",");
        }
        snprintf(piece, sizeof(piece), "%ld", indices[i] + 1);
        strcat(result, piece);
    }

    return result;
}

static void rational_append_string_entry(char ***items, slong *count, slong *cap, char *entry) {
    if (!entry) {
        return;
    }

    if (*count >= *cap) {
        slong new_cap = (*cap == 0) ? 8 : (*cap * 2);
        char **new_items = (char **) realloc(*items, new_cap * sizeof(char *));
        if (!new_items) {
            free(entry);
            return;
        }
        *items = new_items;
        *cap = new_cap;
    }

    (*items)[*count] = entry;
    (*count)++;
}

static void rational_store_resultant_step(rational_solutions_t *sols, char *buffer) {
    if (!sols || !buffer) {
        free(buffer);
        return;
    }

    rational_append_string_entry(&sols->resultant_steps,
                                &sols->num_resultant_steps,
                                &sols->resultant_steps_cap,
                                buffer);
}

static void rational_add_resultant_step(rational_solutions_t *sols, const char *fmt, ...) {
    if (!sols) {
        return;
    }

    va_list args;
    va_list args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return;
    }

    char *buffer = (char *) malloc((size_t) needed + 1);
    if (!buffer) {
        va_end(args);
        return;
    }

    vsnprintf(buffer, (size_t) needed + 1, fmt, args);
    va_end(args);

    rational_store_resultant_step(sols, buffer);
    rational_solver_progress("%s", buffer);
}

static char *rational_format_solution_set_line_from_arrays(char **variable_names, slong num_variables,
                                                           fmpq_t **solution_set, slong *solutions_per_var) {
    if (!variable_names || !solution_set || !solutions_per_var || num_variables <= 0) {
        return NULL;
    }

    size_t total_len = 1;
    for (slong var = 0; var < num_variables; var++) {
        total_len += strlen(variable_names[var]) + 16;
        slong num_sols = solutions_per_var[var];
        if (num_sols == 0) {
            total_len += strlen("no solution");
        } else {
            for (slong sol = 0; sol < num_sols; sol++) {
                char *sol_str = fmpq_get_str(NULL, 10, solution_set[var][sol]);
                if (sol_str) {
                    total_len += strlen(sol_str) + 2;
                    flint_free(sol_str);
                }
            }
        }
    }

    char *line = (char *) malloc(total_len);
    if (!line) {
        return NULL;
    }

    line[0] = '\0';
    for (slong var = 0; var < num_variables; var++) {
        slong num_sols = solutions_per_var[var];

        if (var > 0) {
            strcat(line, ", ");
        }
        strcat(line, variable_names[var]);
        strcat(line, " = ");

        if (num_sols == 0) {
            strcat(line, "no solution");
        } else if (num_sols == 1) {
            char *sol_str = fmpq_get_str(NULL, 10, solution_set[var][0]);
            if (sol_str) {
                strcat(line, sol_str);
                flint_free(sol_str);
            }
        } else {
            strcat(line, "{");
            for (slong sol = 0; sol < num_sols; sol++) {
                char *sol_str = fmpq_get_str(NULL, 10, solution_set[var][sol]);
                if (sol > 0) {
                    strcat(line, ", ");
                }
                if (sol_str) {
                    strcat(line, sol_str);
                    flint_free(sol_str);
                }
            }
            strcat(line, "}");
        }
    }

    return line;
}

static void rational_add_candidate_solution(rational_solutions_t *sols, char *line, int pass) {
    if (!sols || !line) {
        return;
    }

    if (sols->num_candidate_solution_lines >= sols->candidate_solution_lines_cap) {
        slong new_cap = (sols->candidate_solution_lines_cap == 0)
                        ? 8 : (sols->candidate_solution_lines_cap * 2);
        char **new_lines = (char **) realloc(sols->candidate_solution_lines,
                                             new_cap * sizeof(char *));
        int *new_pass = (int *) realloc(sols->candidate_solution_pass,
                                        new_cap * sizeof(int));
        if (!new_lines || !new_pass) {
            free(line);
            if (new_lines) sols->candidate_solution_lines = new_lines;
            if (new_pass) sols->candidate_solution_pass = new_pass;
            return;
        }
        sols->candidate_solution_lines = new_lines;
        sols->candidate_solution_pass = new_pass;
        sols->candidate_solution_lines_cap = new_cap;
    }

    sols->candidate_solution_lines[sols->num_candidate_solution_lines] = line;
    sols->candidate_solution_pass[sols->num_candidate_solution_lines] = pass;
    sols->num_candidate_solution_lines++;
}

static void rational_merge_solver_logs(rational_solutions_t *dst, const rational_solutions_t *src) {
    if (!dst || !src) {
        return;
    }

    for (slong i = 0; i < src->num_resultant_steps; i++) {
        rational_store_resultant_step(dst, strdup(src->resultant_steps[i]));
    }
}

int rational_contains_variable(const char *poly_str, const char *var_name) {
    const char *ptr = poly_str;
    size_t var_len = strlen(var_name);
    
    while ((ptr = strstr(ptr, var_name)) != NULL) {
        if ((ptr == poly_str || !isalnum(*(ptr-1))) && 
            !isalnum(*(ptr + var_len))) {
            return 1;
        }
        ptr++;
    }
    return 0;
}

int rational_contains_identifier_improved(const char *poly_str, const char *identifier) {
    const char *ptr = poly_str;
    size_t id_len = strlen(identifier);
    
    while ((ptr = strstr(ptr, identifier)) != NULL) {
        if ((ptr == poly_str || !isalnum(*(ptr-1))) && 
            !isalnum(*(ptr + id_len))) {
            return 1;
        }
        ptr++;
    }
    return 0;
}

char** rational_extract_variables_improved(char **poly_strings, slong num_polys, slong *num_vars_out) {
    char **temp_vars = (char**) malloc(100 * sizeof(char*));
    slong temp_count = 0;
    
    printf("=== Improved Rational Variable Extraction ===\n");
    
    for (slong poly_idx = 0; poly_idx < num_polys; poly_idx++) {
        const char *poly = poly_strings[poly_idx];
        size_t len = strlen(poly);
        
        for (size_t i = 0; i < len; i++) {
            if (isalpha(poly[i]) || poly[i] == '_') {
                size_t start = i;
                while (i < len && (isalnum(poly[i]) || poly[i] == '_')) {
                    i++;
                }
                
                size_t id_len = i - start;
                char *identifier = (char*) malloc(id_len + 1);
                strncpy(identifier, poly + start, id_len);
                identifier[id_len] = '\0';
                
                if (isdigit(identifier[0])) {
                    free(identifier);
                    continue;
                }
                
                int already_exists = 0;
                for (slong j = 0; j < temp_count; j++) {
                    if (strcmp(temp_vars[j], identifier) == 0) {
                        already_exists = 1;
                        break;
                    }
                }
                
                if (!already_exists) {
                    temp_vars[temp_count] = strdup(identifier);
                    temp_count++;
                    printf("Found variable: %s\n", identifier);
                }
                
                free(identifier);
                i--;
            }
        }
    }
    
    char **final_vars = NULL;
    if (temp_count > 0) {
        final_vars = (char**) malloc(temp_count * sizeof(char*));
        for (slong i = 0; i < temp_count; i++) {
            final_vars[i] = temp_vars[i];
        }
    }
    
    free(temp_vars);
    *num_vars_out = temp_count;
    
    printf("Total variables found: %ld\n", temp_count);
    for (slong i = 0; i < temp_count; i++) {
        printf("  Variable %ld: %s\n", i, final_vars[i]);
    }
    
    return final_vars;
}

slong rational_get_variable_max_degree_in_polynomial(const char *poly_str, const char *var_name) {
    const char *ptr = poly_str;
    slong max_degree = 0;
    size_t var_len = strlen(var_name);
    
    while ((ptr = strstr(ptr, var_name)) != NULL) {
        if ((ptr == poly_str || !isalnum(*(ptr-1))) && 
            !isalnum(*(ptr + var_len))) {
            
            const char *exp_ptr = ptr + var_len;
            while (isspace(*exp_ptr)) exp_ptr++;
            
            slong degree = 1;
            if (*exp_ptr == '^') {
                exp_ptr++;
                while (isspace(*exp_ptr)) exp_ptr++;
                if (isdigit(*exp_ptr)) {
                    degree = strtol(exp_ptr, NULL, 10);
                }
            }
            
            if (degree > max_degree) {
                max_degree = degree;
            }
        }
        ptr++;
    }
    
    return max_degree;
}

static int rational_compare_variables_by_degree(const void *a, const void *b) {
    const rational_variable_info_t *va = (const rational_variable_info_t*)a;
    const rational_variable_info_t *vb = (const rational_variable_info_t*)b;
    
    if (va->max_degree < vb->max_degree) return -1;
    if (va->max_degree > vb->max_degree) return 1;
    return strcmp(va->name, vb->name);
}

void rational_generate_equation_combinations(slong num_equations, slong target_equations, 
                                             rational_equation_combination_t **combinations, slong *num_combinations) {
    slong total_combinations = 1;
    for (slong i = 0; i < target_equations; i++) {
        total_combinations = total_combinations * (num_equations - i) / (i + 1);
    }
    
    *num_combinations = total_combinations;
    *combinations = (rational_equation_combination_t*) malloc(total_combinations * sizeof(rational_equation_combination_t));
    
    slong comb_idx = 0;
    slong *current_combination = (slong*) malloc(target_equations * sizeof(slong));
    
    for (slong i = 0; i < target_equations; i++) {
        current_combination[i] = i;
    }
    
    while (comb_idx < total_combinations) {
        (*combinations)[comb_idx].equation_indices = (slong*) malloc(target_equations * sizeof(slong));
        (*combinations)[comb_idx].num_equations = target_equations;
        (*combinations)[comb_idx].tried = 0;
        (*combinations)[comb_idx].success = 0;
        
        for (slong i = 0; i < target_equations; i++) {
            (*combinations)[comb_idx].equation_indices[i] = current_combination[i];
        }
        comb_idx++;
        
        slong k = target_equations - 1;
        while (k >= 0 && current_combination[k] == num_equations - target_equations + k) {
            k--;
        }
        
        if (k < 0) break;
        
        current_combination[k]++;
        for (slong i = k + 1; i < target_equations; i++) {
            current_combination[i] = current_combination[i-1] + 1;
        }
    }
    
    free(current_combination);
}

void rational_free_equation_combinations(rational_equation_combination_t *combinations, slong num_combinations) {
    if (combinations) {
        for (slong i = 0; i < num_combinations; i++) {
            if (combinations[i].equation_indices) {
                free(combinations[i].equation_indices);
            }
        }
        free(combinations);
    }
}

void rational_solutions_init(rational_solutions_t *sols, slong num_vars) {
    sols->num_variables = num_vars;
    sols->is_valid = 0;
    sols->has_no_solutions = 0;
    sols->error_message = NULL;
    sols->num_solution_sets = 0;
    sols->num_equations = 0;
    sols->variable_order = NULL;
    sols->elimination_summary = NULL;
    sols->total_combinations = 0;
    sols->successful_combinations = 0;
    sols->num_base_solutions = 0;
    sols->checked_solution_sets = -1;
    sols->verified_solution_sets = -1;
    sols->resultant_steps = NULL;
    sols->num_resultant_steps = 0;
    sols->resultant_steps_cap = 0;
    sols->candidate_solution_lines = NULL;
    sols->candidate_solution_pass = NULL;
    sols->num_candidate_solution_lines = 0;
    sols->candidate_solution_lines_cap = 0;
    
    if (num_vars > 0) {
        sols->variable_names = (char**) calloc(num_vars, sizeof(char*));
        sols->solution_sets = NULL;
        sols->solutions_per_var = NULL;
    } else {
        sols->variable_names = NULL;
        sols->solution_sets = NULL;
        sols->solutions_per_var = NULL;
    }
}

void rational_solutions_clear(rational_solutions_t *sols) {
    if (sols->variable_names) {
        for (slong i = 0; i < sols->num_variables; i++) {
            if (sols->variable_names[i]) {
                free(sols->variable_names[i]);
            }
        }
        free(sols->variable_names);
    }
    
    if (sols->solution_sets) {
        for (slong set = 0; set < sols->num_solution_sets; set++) {
            if (sols->solution_sets[set]) {
                for (slong var = 0; var < sols->num_variables; var++) {
                    if (sols->solution_sets[set][var]) {
                        for (slong sol = 0; sol < sols->solutions_per_var[set * sols->num_variables + var]; sol++) {
                            fmpq_clear(sols->solution_sets[set][var][sol]);
                        }
                        free(sols->solution_sets[set][var]);
                    }
                }
                free(sols->solution_sets[set]);
            }
        }
        free(sols->solution_sets);
    }
    
    if (sols->solutions_per_var) {
        free(sols->solutions_per_var);
    }
    
    if (sols->error_message) {
        free(sols->error_message);
    }

    if (sols->variable_order) {
        free(sols->variable_order);
    }

    if (sols->elimination_summary) {
        free(sols->elimination_summary);
    }

    if (sols->resultant_steps) {
        for (slong i = 0; i < sols->num_resultant_steps; i++) {
            free(sols->resultant_steps[i]);
        }
        free(sols->resultant_steps);
    }

    if (sols->candidate_solution_lines) {
        for (slong i = 0; i < sols->num_candidate_solution_lines; i++) {
            free(sols->candidate_solution_lines[i]);
        }
        free(sols->candidate_solution_lines);
    }

    if (sols->candidate_solution_pass) {
        free(sols->candidate_solution_pass);
    }
    
    memset(sols, 0, sizeof(rational_solutions_t));
}

void print_rational_solutions(const rational_solutions_t *sols) {
    if (!sols) {
        printf("No solution data available.\n");
        return;
    }

    if (sols->num_resultant_steps > 0) {
        for (slong i = 0; i < sols->num_resultant_steps; i++) {
            printf("Progress: %s\n", sols->resultant_steps[i]);
        }
        printf("\n");
    }

    if (!sols->is_valid) {
        printf("Solve failed");
        if (sols->error_message) {
            printf(": %s", sols->error_message);
        }
        printf("\n");
        return;
    }

    if (sols->has_no_solutions == -1) {
        printf("System has positive dimension.\n");
        return;
    }

    if (sols->has_no_solutions == 1) {
        printf("No solutions over the rational numbers.\n");
        return;
    }

    if (sols->num_variables == 0) {
        printf("No variables.\n");
        return;
    }

    if (sols->num_solution_sets == 0) {
        printf("No solutions found.\n");
        return;
    }

    if (sols->num_candidate_solution_lines > 0) {
        printf("Candidate sets:\n");
        for (slong i = 0; i < sols->num_candidate_solution_lines; i++) {
            printf("  [%ld] %s [%s]\n",
                   i + 1,
                   sols->candidate_solution_lines[i],
                   sols->candidate_solution_pass[i] ? "PASS" : "FAIL");
        }
    }
    printf("Found %ld solution set(s):\n", sols->num_solution_sets);

    for (slong set = 0; set < sols->num_solution_sets; set++) {
        printf("  [%ld] ", set + 1);
        for (slong var = 0; var < sols->num_variables; var++) {
            slong num_sols = sols->solutions_per_var[set * sols->num_variables + var];

            if (var > 0) {
                printf(", ");
            }

            printf("%s = ", sols->variable_names[var]);
            if (num_sols == 0) {
                printf("no solution");
            } else if (num_sols == 1) {
                fmpq_print(sols->solution_sets[set][var][0]);
            } else {
                printf("{");
                for (slong sol = 0; sol < num_sols; sol++) {
                    if (sol > 0) printf(", ");
                    fmpq_print(sols->solution_sets[set][var][sol]);
                }
                printf("}");
            }
        }
        printf("\n");
    }
}

int rational_extract_and_sort_variables(char **poly_strings, slong num_polys,
                                         rational_variable_info_t **sorted_vars_out, slong *num_vars_out) {
    
    printf("=== Analyzing Rational Polynomial Variables (improved) ===\n");
    
    char **all_vars = rational_extract_variables_improved(poly_strings, num_polys, num_vars_out);
    
    if (*num_vars_out == 0) {
        *sorted_vars_out = NULL;
        return 1;
    }
    
    for (slong i = 0; i < num_polys; i++) {
        slong total_degree = 0;
        
        for (slong j = 0; j < *num_vars_out; j++) {
            slong var_degree = rational_get_variable_max_degree_in_polynomial(poly_strings[i], all_vars[j]);
            if (var_degree > total_degree) {
                total_degree = var_degree;
            }
        }
        
        printf("Polynomial %ld: estimated degree = %ld\n", i + 1, total_degree);
    }
    
    printf("Discovered variables (%ld): ", *num_vars_out);
    for (slong i = 0; i < *num_vars_out; i++) {
        if (i > 0) printf(", ");
        printf("%s", all_vars[i]);
    }
    printf("\n");
    
    rational_variable_info_t *var_info = (rational_variable_info_t*) malloc(*num_vars_out * sizeof(rational_variable_info_t));
    
    for (slong i = 0; i < *num_vars_out; i++) {
        var_info[i].name = strdup(all_vars[i]);
        var_info[i].index = i;
        var_info[i].max_degree = 0;
        
        for (slong j = 0; j < num_polys; j++) {
            slong degree = rational_get_variable_max_degree_in_polynomial(poly_strings[j], all_vars[i]);
            if (degree > var_info[i].max_degree) {
                var_info[i].max_degree = degree;
            }
        }
        
        free(all_vars[i]);
    }
    
    free(all_vars);
    
    qsort(var_info, *num_vars_out, sizeof(rational_variable_info_t), rational_compare_variables_by_degree);
    
    printf("\nVariables sorted by maximum degree:\n");
    for (slong i = 0; i < *num_vars_out; i++) {
        printf("  %s: max degree %ld\n", var_info[i].name, var_info[i].max_degree);
    }
    
    *sorted_vars_out = var_info;
    return 1;
}

static int rational_parse_string_to_fmpq_poly(const char *poly_str, const char *var_name,
                                               fmpq_poly_t poly) {
    fmpq_poly_init(poly);
    fmpq_poly_zero(poly);
    
    if (strcmp(poly_str, "0") == 0 || strcmp(poly_str, "") == 0) {
        return 1;
    }
    
    const char *ptr = poly_str;
    fmpq_t coeff, exp_val;
    fmpq_init(coeff);
    fmpq_init(exp_val);
    
    while (*ptr) {
        while (isspace(*ptr)) ptr++;
        
        if (*ptr == '\0') break;
        
        // Skip any parentheses
        if (*ptr == '(' || *ptr == ')') {
            ptr++;
            continue;
        }
        
        int sign = 1;
        if (*ptr == '+') {
            ptr++;
            while (isspace(*ptr)) ptr++;
        } else if (*ptr == '-') {
            sign = -1;
            ptr++;
            while (isspace(*ptr)) ptr++;
        }
        
        fmpq_t term_coeff;
        fmpq_init(term_coeff);
        fmpq_set_si(term_coeff, 1, 1);
        
        slong term_degree = 0;
        
        if (isdigit(*ptr) || *ptr == '.') {
            const char *num_start = ptr;
            while (isdigit(*ptr) || *ptr == '.' || *ptr == '/') ptr++;
            size_t num_len = ptr - num_start;
            char *num_str = (char*) malloc(num_len + 1);
            strncpy(num_str, num_start, num_len);
            num_str[num_len] = '\0';
            
            char *slash = strchr(num_str, '/');
            if (slash) {
                *slash = '\0';
                fmpz_t num, den;
                fmpz_init(num);
                fmpz_init(den);
                fmpz_set_str(num, num_str, 10);
                fmpz_set_str(den, slash + 1, 10);
                fmpq_set_fmpz_frac(term_coeff, num, den);
                fmpz_clear(num);
                fmpz_clear(den);
            } else {
                fmpz_t num;
                fmpz_init(num);
                fmpz_set_str(num, num_str, 10);
                fmpq_set_fmpz(term_coeff, num);
                fmpz_clear(num);
            }
            free(num_str);
        }
        
        while (isspace(*ptr)) ptr++;
        
        if (*ptr == '*') {
            ptr++;
            while (isspace(*ptr)) ptr++;
        }
        
        if (strncmp(ptr, var_name, strlen(var_name)) == 0) {
            ptr += strlen(var_name);
            term_degree = 1;
            
            while (isspace(*ptr)) ptr++;
            if (*ptr == '^') {
                ptr++;
                while (isspace(*ptr)) ptr++;
                if (isdigit(*ptr)) {
                    term_degree = strtol(ptr, (char**)&ptr, 10);
                }
            }
        } else if (*ptr == '^') {
            // Skip constant exponentiation: const^exp is still a const
            ptr++;
            while (isspace(*ptr)) ptr++;
            if (isdigit(*ptr)) {
                while (isdigit(*ptr)) ptr++;
            }
        }
        
        if (sign < 0) {
            fmpq_neg(term_coeff, term_coeff);
        }
        
        // Add to the existing coefficient instead of replacing
        fmpq_t existing_coeff;
        fmpq_init(existing_coeff);
        fmpq_poly_get_coeff_fmpq(existing_coeff, poly, term_degree);
        fmpq_add(existing_coeff, existing_coeff, term_coeff);
        fmpq_poly_set_coeff_fmpq(poly, term_degree, existing_coeff);
        fmpq_clear(existing_coeff);
        
        fmpq_clear(term_coeff);
    }
    
    fmpq_clear(coeff);
    fmpq_clear(exp_val);
    
    return 1;
}

fmpq_t* rational_solve_univariate_equation_all_roots(const char *poly_str, const char *var_name,
                                                      slong *num_roots_out) {
    printf("Solving univariate equation: %s (variable: %s)\n", poly_str, var_name);
    
    fmpq_poly_t fmpq_poly;
    rational_parse_string_to_fmpq_poly(poly_str, var_name, fmpq_poly);
    
    fmpq_roots_t roots;
    fmpq_roots_init(&roots);
    slong num_roots = fmpq_poly_roots(&roots, fmpq_poly, 0);
    
    fmpq_t *result_roots = NULL;
    if (num_roots > 0) {
        result_roots = (fmpq_t*) malloc(roots.num_roots * sizeof(fmpq_t));
        for (slong i = 0; i < roots.num_roots; i++) {
            fmpq_init(result_roots[i]);
            fmpq_set(result_roots[i], roots.roots[i]);
        }
        *num_roots_out = roots.num_roots;
        
        printf("Found %ld rational roots:\n", roots.num_roots);
        for (slong i = 0; i < roots.num_roots; i++) {
            printf("  Root %ld: ", i + 1);
            fmpq_print(roots.roots[i]);
            printf("\n");
        }
    } else {
        *num_roots_out = 0;
        printf("No rational roots found.\n");
    }
    
    fmpq_roots_clear(&roots);
    fmpq_poly_clear(fmpq_poly);
    
    return result_roots;
}

char* rational_substitute_variable_in_polynomial(const char *poly_str, const char *var_name,
                                                  const fmpq_t value) {
    char *result = (char*) malloc(strlen(poly_str) + 100);
    result[0] = '\0';
    const char *ptr = poly_str;
    
    while (*ptr) {
        size_t var_len = strlen(var_name);
        if (strncmp(ptr, var_name, var_len) == 0 &&
            (ptr == poly_str || !isalnum(*(ptr-1))) &&
            !isalnum(*(ptr + var_len))) {
            char *val_str = fmpq_get_str(NULL, 10, value);
            strcat(result, val_str);
            flint_free(val_str);
            ptr += var_len;
        } else {
            size_t len = strlen(result);
            result[len] = *ptr;
            result[len + 1] = '\0';
            ptr++;
        }
    }
    
    return result;
}

char* rational_eliminate_variable_dixon_with_selection(char **poly_strings, slong num_polys, 
                                                        const char **elim_vars, slong num_elim_vars,
                                                        rational_equation_combination_t *combination) {
    printf("  Entering rational_eliminate_variable_dixon_with_selection...\n");
    printf("    combination->num_equations = %ld, num_elim_vars = %ld\n", combination->num_equations, num_elim_vars);
    fflush(stdout);
    
    if (combination->num_equations == num_elim_vars + 1) {
        printf("    Using rational dixon_str_rational for elimination\n");
        fflush(stdout);
        
        size_t total_poly_len = 0;
        for (slong i = 0; i < combination->num_equations; i++) {
            total_poly_len += strlen(poly_strings[combination->equation_indices[i]]) + 2;
        }
        char *combined_polys = (char*) malloc(total_poly_len + 1);
        combined_polys[0] = '\0';
        for (slong i = 0; i < combination->num_equations; i++) {
            if (i > 0) {
                strcat(combined_polys, ", ");
            }
            strcat(combined_polys, poly_strings[combination->equation_indices[i]]);
        }
        
        size_t total_vars_len = 0;
        for (slong i = 0; i < num_elim_vars; i++) {
            total_vars_len += strlen(elim_vars[i]) + 2;
        }
        char *combined_vars = (char*) malloc(total_vars_len + 1);
        combined_vars[0] = '\0';
        for (slong i = 0; i < num_elim_vars; i++) {
            if (i > 0) {
                strcat(combined_vars, ", ");
            }
            strcat(combined_vars, elim_vars[i]);
        }
        
        printf("    Calling dixon_str_rational_with_file...\n");
        printf("    Polys: %s\n", combined_polys);
        printf("    Elim:  %s\n", combined_vars);
        fflush(stdout);
        
        char *result = dixon_str_rational_with_file(combined_polys, combined_vars, NULL);
        
        printf("    dixon_str_rational_with_file returned: %s\n", result ? result : "NULL");
        fflush(stdout);
        
        free(combined_polys);
        free(combined_vars);
        return result;
    } else {
        printf("    Number of equations does not match, returning 0\n");
        fflush(stdout);
        return strdup("0");
    }
}

int rational_verify_solution_set(char **original_polys, slong num_polys,
                                 rational_variable_info_t *sorted_vars, slong num_vars,
                                 fmpq_t **solution_values) {
    printf("    Verifying rational solution: ");
    for (slong v = 0; v < num_vars; v++) {
        if (v > 0) printf(", ");
        printf("%s=", sorted_vars[v].name);
        fmpq_print(solution_values[v][0]);
    }
    printf("\n");
    
    for (slong poly_idx = 0; poly_idx < num_polys; poly_idx++) {
        char *current_poly = strdup(original_polys[poly_idx]);
        
        for (slong var_idx = 0; var_idx < num_vars; var_idx++) {
            char *next_poly = rational_substitute_variable_in_polynomial(current_poly,
                                                                       sorted_vars[var_idx].name,
                                                                       solution_values[var_idx][0]);
            free(current_poly);
            current_poly = next_poly;
        }
        
        printf("    Equation %ld after substitution: %s\n", poly_idx + 1, current_poly);
        
        fmpq_poly_t result_poly;
        fmpq_poly_init(result_poly);
        rational_parse_string_to_fmpq_poly(current_poly, "x", result_poly);
        
        int is_zero = (fmpq_poly_degree(result_poly) < 0);
        if (!is_zero) {
            fmpq_t const_coeff;
            fmpq_init(const_coeff);
            fmpq_poly_get_coeff_fmpq(const_coeff, result_poly, 0);
            is_zero = fmpq_is_zero(const_coeff);
            fmpq_clear(const_coeff);
        }
        
        fmpq_poly_clear(result_poly);
        
        if (!is_zero) {
            printf("    Verification FAILED for equation %ld: %s != 0\n", poly_idx + 1, current_poly);
            free(current_poly);
            return 0;
        }
        
        free(current_poly);
    }
    
    printf("    Verification PASSED\n");
    return 1;
}

void rational_filter_solutions_by_verification(rational_solutions_t *sols,
                                                char **original_polys, slong num_polys,
                                                rational_variable_info_t *sorted_vars) {
    if (!sols->is_valid || sols->has_no_solutions || sols->num_solution_sets == 0) {
        return;
    }
    
    printf("\n=== Verifying Rational Solution Sets ===\n");
    
    slong verified_count = 0;
    int *valid_sets = (int*) calloc(sols->num_solution_sets, sizeof(int));
    
    for (slong set = 0; set < sols->num_solution_sets; set++) {
        printf("  Checking rational solution set %ld...\n", set + 1);
        int pass = 0;
        char *candidate_line = rational_format_solution_set_line_from_arrays(
            sols->variable_names,
            sols->num_variables,
            sols->solution_sets[set],
            &sols->solutions_per_var[set * sols->num_variables]
        );
        
        int is_complete = 1;
        for (slong var = 0; var < sols->num_variables; var++) {
            if (sols->solutions_per_var[set * sols->num_variables + var] == 0) {
                printf("    Solution set %ld incomplete (missing value for %s)\n", 
                       set + 1, sols->variable_names[var]);
                is_complete = 0;
                break;
            }
        }
        
        if (!is_complete) {
            rational_add_candidate_solution(sols, candidate_line, 0);
            continue;
        }
        
        if (rational_verify_solution_set(original_polys, num_polys, sorted_vars, sols->num_variables,
                                        sols->solution_sets[set])) {
            valid_sets[set] = 1;
            verified_count++;
            pass = 1;
        }
        rational_add_candidate_solution(sols, candidate_line, pass);
    }
    
    printf("  Verification complete: %ld out of %ld solution sets are valid\n", 
           verified_count, sols->num_solution_sets);
    sols->checked_solution_sets = sols->num_solution_sets;
    sols->verified_solution_sets = verified_count;
    
    if (verified_count == 0) {
        sols->has_no_solutions = 1;
        
        if (sols->solution_sets) {
            for (slong set = 0; set < sols->num_solution_sets; set++) {
                if (sols->solution_sets[set]) {
                    for (slong var = 0; var < sols->num_variables; var++) {
                        if (sols->solution_sets[set][var]) {
                            for (slong sol = 0; sol < sols->solutions_per_var[set * sols->num_variables + var]; sol++) {
                                fmpq_clear(sols->solution_sets[set][var][sol]);
                            }
                            free(sols->solution_sets[set][var]);
                        }
                    }
                    free(sols->solution_sets[set]);
                }
            }
            free(sols->solution_sets);
            sols->solution_sets = NULL;
        }
        
        if (sols->solutions_per_var) {
            free(sols->solutions_per_var);
            sols->solutions_per_var = NULL;
        }
        
        sols->num_solution_sets = 0;
        
    } else if (verified_count < sols->num_solution_sets) {
        fmpq_t ***new_solution_sets = (fmpq_t***) malloc(verified_count * sizeof(fmpq_t**));
        slong *new_solutions_per_var = (slong*) calloc(verified_count * sols->num_variables, sizeof(slong));
        
        slong new_idx = 0;
        for (slong set = 0; set < sols->num_solution_sets; set++) {
            if (valid_sets[set]) {
                new_solution_sets[new_idx] = sols->solution_sets[set];
                for (slong var = 0; var < sols->num_variables; var++) {
                    new_solutions_per_var[new_idx * sols->num_variables + var] = 
                        sols->solutions_per_var[set * sols->num_variables + var];
                }
                sols->solution_sets[set] = NULL;
                new_idx++;
            } else {
                if (sols->solution_sets[set]) {
                    for (slong var = 0; var < sols->num_variables; var++) {
                        if (sols->solution_sets[set][var]) {
                            for (slong sol = 0; sol < sols->solutions_per_var[set * sols->num_variables + var]; sol++) {
                                fmpq_clear(sols->solution_sets[set][var][sol]);
                            }
                            free(sols->solution_sets[set][var]);
                        }
                    }
                    free(sols->solution_sets[set]);
                }
            }
        }
        
        free(sols->solution_sets);
        free(sols->solutions_per_var);
        sols->solution_sets = new_solution_sets;
        sols->solutions_per_var = new_solutions_per_var;
        sols->num_solution_sets = verified_count;
    }
    
    free(valid_sets);
    printf("=== Verification Complete ===\n\n");
}

int rational_solve_by_back_substitution_recursive_enhanced(char **original_polys, slong num_polys,
                                                             rational_variable_info_t *sorted_vars, slong num_vars,
                                                             fmpq_t *base_solutions, slong num_base_solutions,
                                                             rational_solutions_t *sols) {
    if (sols->has_no_solutions == -1) {
        printf("Back substitution SKIPPED: system already has dimension > 0 status\n");
        return 1;
    }
    
    if (num_vars <= 1) {
        return 1; 
    }
    
    printf("Starting rational back substitution process...\n");
    printf("Base variable %s has %ld solutions\n", sorted_vars[0].name, num_base_solutions);
    
    fmpq_t ***all_solution_sets = NULL;
    slong *all_solutions_per_var = NULL;
    slong total_solution_sets = 0;
    
    for (slong base_idx = 0; base_idx < num_base_solutions; base_idx++) {
        printf("\n--- Processing rational base solution %ld: ", base_idx + 1);
        fmpq_print(base_solutions[base_idx]);
        printf(" ---\n");
        
        char **reduced_polys = (char**) malloc(num_polys * sizeof(char*));
        slong num_nonzero_polys = 0;
        
        for (slong poly_idx = 0; poly_idx < num_polys; poly_idx++) {
            char *substituted = rational_substitute_variable_in_polynomial(original_polys[poly_idx],
                                                                          sorted_vars[0].name,
                                                                          base_solutions[base_idx]);
            printf("Equation %ld after substituting %s: %s\n", 
                   poly_idx, sorted_vars[0].name, substituted);
            
            if (strcmp(substituted, "0") != 0) {
                reduced_polys[num_nonzero_polys] = substituted;
                num_nonzero_polys++;
            } else {
                printf("Equation %ld became zero (satisfied)\n", poly_idx);
                free(substituted);
            }
        }
        
        if (num_nonzero_polys == 0) {
            printf("All equations satisfied by base solution\n");
            if (total_solution_sets == 0) {
                all_solution_sets = (fmpq_t***) malloc(sizeof(fmpq_t**));
                all_solutions_per_var = (slong*) calloc(num_vars, sizeof(slong));
            } else {
                all_solution_sets = (fmpq_t***) realloc(all_solution_sets, 
                                                          (total_solution_sets + 1) * sizeof(fmpq_t**));
                all_solutions_per_var = (slong*) realloc(all_solutions_per_var,
                                                         (total_solution_sets + 1) * num_vars * sizeof(slong));
            }
            
            all_solution_sets[total_solution_sets] = (fmpq_t**) calloc(num_vars, sizeof(fmpq_t*));
            all_solution_sets[total_solution_sets][0] = (fmpq_t*) malloc(sizeof(fmpq_t));
            fmpq_init(all_solution_sets[total_solution_sets][0][0]);
            fmpq_set(all_solution_sets[total_solution_sets][0][0], base_solutions[base_idx]);
            all_solutions_per_var[total_solution_sets * num_vars + 0] = 1;
            
            for (slong v = 1; v < num_vars; v++) {
                all_solutions_per_var[total_solution_sets * num_vars + v] = 0;
            }
            
            total_solution_sets++;
            free(reduced_polys);
            continue;
        }
        
        slong target_equations = num_vars - 1;
        
        if (num_nonzero_polys >= target_equations) {
            rational_equation_combination_t *combinations = NULL;
            slong num_combinations = 0;
            
            if (num_nonzero_polys == target_equations) {
                num_combinations = 1;
                combinations = (rational_equation_combination_t*) malloc(sizeof(rational_equation_combination_t));
                combinations[0].equation_indices = (slong*) malloc(target_equations * sizeof(slong));
                combinations[0].num_equations = target_equations;
                for (slong i = 0; i < target_equations; i++) {
                    combinations[0].equation_indices[i] = i;
                }
            } else {
                rational_generate_equation_combinations(num_nonzero_polys, target_equations, 
                                                         &combinations, &num_combinations);
            }
            
            printf("Generated %ld equation combinations to try\n", num_combinations);
            
            int found_finite_solution = 0;
            
            for (slong comb_idx = 0; comb_idx < num_combinations; comb_idx++) {
                printf("  Trying combination %ld: equations ", comb_idx + 1);
                for (slong i = 0; i < target_equations; i++) {
                    if (i > 0) printf(", ");
                    printf("%ld", combinations[comb_idx].equation_indices[i]);
                }
                printf("\n");
                
                char **final_polys = (char**) malloc(target_equations * sizeof(char*));
                for (slong i = 0; i < target_equations; i++) {
                    final_polys[i] = strdup(reduced_polys[combinations[comb_idx].equation_indices[i]]);
                }

                {
                    char *base_str = fmpq_get_str(NULL, 10, base_solutions[base_idx]);
                    char *eq_list = rational_format_equation_index_list(combinations[comb_idx].equation_indices,
                                                               target_equations);
                    if (target_equations == 1) {
                        rational_add_resultant_step(sols,
                                           "Base %s = %s -> solve reduced eq(%s)",
                                           sorted_vars[0].name,
                                           base_str ? base_str : "?",
                                           eq_list ? eq_list : "?");
                    } else {
                        rational_add_resultant_step(sols,
                                           "Base %s = %s -> compute reduced resultants from eq(%s)",
                                           sorted_vars[0].name,
                                           base_str ? base_str : "?",
                                           eq_list ? eq_list : "?");
                    }
                    if (eq_list) free(eq_list);
                    if (base_str) flint_free(base_str);
                }
                
                rational_variable_info_t *remaining_vars = (rational_variable_info_t*) malloc((num_vars - 1) * sizeof(rational_variable_info_t));
                for (slong i = 1; i < num_vars; i++) {
                    remaining_vars[i - 1] = sorted_vars[i];
                }
                
                rational_solutions_t *test_sols = solve_rational_polynomial_system_array_with_vars(final_polys, 
                                                                                           target_equations,
                                                                                           remaining_vars,
                                                                                           num_vars - 1);
                
                if (test_sols && test_sols->is_valid) {
                    rational_merge_solver_logs(sols, test_sols);
                    if (test_sols->has_no_solutions == -1) {
                        printf("  Combination %ld: dimension > 0\n", comb_idx + 1);
                    } else if (test_sols->has_no_solutions == 1) {
                        printf("  Combination %ld: no solutions\n", comb_idx + 1);
                    } else {
                        printf("  ✓ Combination %ld succeeded with finite solutions!\n", comb_idx + 1);
                        found_finite_solution++;
                        
                        if (test_sols->num_solution_sets > 0) {
                            slong old_total = total_solution_sets;
                            total_solution_sets += test_sols->num_solution_sets;
                            
                            if (old_total == 0) {
                                all_solution_sets = (fmpq_t***) malloc(total_solution_sets * sizeof(fmpq_t**));
                                all_solutions_per_var = (slong*) calloc(total_solution_sets * num_vars, sizeof(slong));
                            } else {
                                all_solution_sets = (fmpq_t***) realloc(all_solution_sets, 
                                                                          total_solution_sets * sizeof(fmpq_t**));
                                all_solutions_per_var = (slong*) realloc(all_solutions_per_var,
                                                                         total_solution_sets * num_vars * sizeof(slong));
                            }
                            
                            for (slong ts = 0; ts < test_sols->num_solution_sets; ts++) {
                                slong new_set_idx = old_total + ts;
                                all_solution_sets[new_set_idx] = (fmpq_t**) calloc(num_vars, sizeof(fmpq_t*));
                                
                                all_solution_sets[new_set_idx][0] = (fmpq_t*) malloc(sizeof(fmpq_t));
                                fmpq_init(all_solution_sets[new_set_idx][0][0]);
                                fmpq_set(all_solution_sets[new_set_idx][0][0], base_solutions[base_idx]);
                                all_solutions_per_var[new_set_idx * num_vars + 0] = 1;
                                
                                for (slong v = 1; v < num_vars; v++) {
                                    slong src_var_idx = v - 1;
                                    slong num_sols = test_sols->solutions_per_var[ts * (num_vars - 1) + src_var_idx];
                                    all_solutions_per_var[new_set_idx * num_vars + v] = num_sols;
                                    if (num_sols > 0) {
                                        all_solution_sets[new_set_idx][v] = (fmpq_t*) malloc(num_sols * sizeof(fmpq_t));
                                        for (slong s = 0; s < num_sols; s++) {
                                            fmpq_init(all_solution_sets[new_set_idx][v][s]);
                                            fmpq_set(all_solution_sets[new_set_idx][v][s], 
                                                     test_sols->solution_sets[ts][src_var_idx][s]);
                                        }
                                    } else {
                                        all_solution_sets[new_set_idx][v] = NULL;
                                    }
                                }
                            }
                        }
                    }
                }
                
                rational_solutions_clear(test_sols);
                free(test_sols);
                
                for (slong i = 0; i < target_equations; i++) {
                    free(final_polys[i]);
                }
                free(final_polys);
                free(remaining_vars);
                
                if (found_finite_solution > 0) {
                    break;
                }
            }
            
            rational_free_equation_combinations(combinations, num_combinations);
        }
        
        for (slong i = 0; i < num_nonzero_polys; i++) {
            free(reduced_polys[i]);
        }
        free(reduced_polys);
    }
    
    if (total_solution_sets > 0) {
        sols->solution_sets = all_solution_sets;
        sols->solutions_per_var = all_solutions_per_var;
        sols->num_solution_sets = total_solution_sets;
    }
    
    return 1;
}

int rational_solve_by_elimination_enhanced(char **poly_strings, slong num_polys,
                                             rational_variable_info_t *sorted_vars, slong num_vars,
                                             rational_solutions_t *sols) {
    printf("\n=== Rational Elimination Solver ===\n");
    printf("Number of variables: %ld\n", num_vars);
    printf("Number of equations: %ld\n", num_polys);
    
    printf("Sorted variables order:\n");
    for (slong i = 0; i < num_vars; i++) {
        printf("  [%ld] %s (max_degree=%ld)\n", i, sorted_vars[i].name, sorted_vars[i].max_degree);
    }
    fflush(stdout);
    
    if (num_vars == 0) {
        printf("No variables to solve for.\n");
        return 1;
    }
    
    if (num_vars == 1) {
        printf("Single variable case: solving directly\n");
        slong num_roots = 0;
        fmpq_t *roots = rational_solve_univariate_equation_all_roots(poly_strings[0], sorted_vars[0].name, &num_roots);
        
        if (num_roots > 0) {
            sols->solution_sets = (fmpq_t***) malloc(sizeof(fmpq_t**));
            sols->solution_sets[0] = (fmpq_t**) calloc(1, sizeof(fmpq_t*));
            sols->solution_sets[0][0] = roots;
            sols->solutions_per_var = (slong*) calloc(1, sizeof(slong));
            sols->solutions_per_var[0] = num_roots;
            sols->num_solution_sets = 1;
        }
        
        return 1;
    }
    
    slong num_elim_vars = num_vars - 1;
    const char **elim_vars = (const char**) malloc(num_elim_vars * sizeof(char*));
    for (slong i = 1; i < num_vars; i++) {
        elim_vars[i - 1] = sorted_vars[i].name;
    }
    char *keep_var = sorted_vars[0].name;
    
    printf("Eliminating variables: ");
    for (slong i = 0; i < num_elim_vars; i++) {
        if (i > 0) printf(", ");
        printf("%s", elim_vars[i]);
    }
    printf("\nKeeping variable: %s\n", keep_var);
    fflush(stdout);
    
    slong target_equations = num_vars;
    
    if (num_polys < target_equations) {
        fprintf(stderr, "Error: Need at least %ld equations for %ld variables, got %ld\n",
                target_equations, num_vars, num_polys);
        free(elim_vars);
        sols->has_no_solutions = -1;
        return 0;
    }
    
    rational_equation_combination_t *combinations = NULL;
    slong num_combinations = 0;
    
    if (num_polys == target_equations) {
        num_combinations = 1;
        combinations = (rational_equation_combination_t*) malloc(sizeof(rational_equation_combination_t));
        combinations[0].equation_indices = (slong*) malloc(target_equations * sizeof(slong));
        combinations[0].num_equations = target_equations;
        for (slong i = 0; i < target_equations; i++) {
            combinations[0].equation_indices[i] = i;
        }
    } else {
        rational_generate_equation_combinations(num_polys, target_equations, 
                                                 &combinations, &num_combinations);
    }
    
    printf("Generated %ld equation combinations to try\n", num_combinations);
    sols->total_combinations = num_combinations;
    
    int success = 0;
    char *working_resultant = NULL;
    slong successful_combinations = 0;
    
    for (slong comb_idx = 0; comb_idx < num_combinations; comb_idx++) {
        printf("\n=== Trying combination %ld ===\n", comb_idx + 1);
        printf("Equations: ");
        for (slong i = 0; i < combinations[comb_idx].num_equations; i++) {
            if (i > 0) printf(", ");
            printf("%ld", combinations[comb_idx].equation_indices[i] + 1);
        }
        printf("\n");
        
        combinations[comb_idx].tried = 1;
        
        char *resultant = rational_eliminate_variable_dixon_with_selection(
            poly_strings, num_polys,
            elim_vars, num_elim_vars,
            &combinations[comb_idx]
        );
        
        printf("Resultant polynomial: %s\n", resultant);
        
        if (resultant && strcmp(resultant, "0") != 0) {
            combinations[comb_idx].success = 1;
            successful_combinations++;
            
            if (!working_resultant) {
                working_resultant = resultant;
            } else {
                free(resultant);
            }
        } else {
            if (resultant) free(resultant);
        }
    }
    
    free(elim_vars);
    
    if (successful_combinations == 0) {
        printf("All %ld equation combinations resulted in zero resultant\n", num_combinations);
        sols->successful_combinations = 0;
        sols->has_no_solutions = -1;
        rational_free_equation_combinations(combinations, num_combinations);
        return 1;
    }
    
    sols->successful_combinations = successful_combinations;
    printf("Found %ld non-zero resultant(s) out of %ld combinations, proceeding with solving...\n", 
           successful_combinations, num_combinations);
    
    if (working_resultant) {
        slong num_roots = 0;
        fmpq_t *base_roots = rational_solve_univariate_equation_all_roots(working_resultant, keep_var, &num_roots);
        sols->num_base_solutions = num_roots;
        
        if (num_roots > 0) {
            rational_solve_by_back_substitution_recursive_enhanced(
                poly_strings, num_polys, sorted_vars, num_vars,
                base_roots, num_roots, sols
            );
            
            for (slong i = 0; i < num_roots; i++) {
                fmpq_clear(base_roots[i]);
            }
            free(base_roots);
            success = 1;
        } else {
            printf("No rational roots found for the resultant.\n");
        }
        
        free(working_resultant);
    }
    
    rational_free_equation_combinations(combinations, num_combinations);
    
    if (!success) {
        sols->has_no_solutions = 1;
    }
    
    return success;
}

rational_solutions_t* solve_rational_polynomial_system_array_with_vars(char **poly_strings, slong num_polys,
                                                                       rational_variable_info_t *original_vars, slong num_original_vars) {
    rational_solutions_t *sols = (rational_solutions_t*) malloc(sizeof(rational_solutions_t));
    rational_solutions_init(sols, num_original_vars);
    
    for (slong i = 0; i < num_original_vars; i++) {
        sols->variable_names[i] = strdup(original_vars[i].name);
    }
    sols->num_equations = num_polys;
    
    rational_solve_by_elimination_enhanced(poly_strings, num_polys, original_vars, num_original_vars, sols);
    
    if (sols->num_solution_sets > 0) {
        rational_filter_solutions_by_verification(sols, poly_strings, num_polys, original_vars);
    }
    
    sols->is_valid = 1;
    return sols;
}

rational_solutions_t* solve_rational_polynomial_system_array(char **poly_strings, slong num_polys) {
    rational_variable_info_t *sorted_vars = NULL;
    slong num_vars = 0;
    
    if (!rational_extract_and_sort_variables(poly_strings, num_polys, &sorted_vars, &num_vars)) {
        return NULL;
    }
    
    rational_solutions_t *result = solve_rational_polynomial_system_array_with_vars(
        poly_strings, num_polys, sorted_vars, num_vars
    );
    
    for (slong i = 0; i < num_vars; i++) {
        free(sorted_vars[i].name);
    }
    free(sorted_vars);
    
    return result;
}

rational_solutions_t* solve_rational_polynomial_system_string(const char *poly_string) {
    slong num_polys = 0;
    char **poly_strings = split_string((char*)poly_string, &num_polys);
    
    if (!poly_strings || num_polys == 0) {
        return NULL;
    }
    
    rational_solutions_t *result = solve_rational_polynomial_system_array(poly_strings, num_polys);
    
    free_split_strings(poly_strings, num_polys);
    
    return result;
}

void test_rational_polynomial_solver(void) {
    printf("\n=== Testing Rational Polynomial System Solver ===\n");
    
    char *test1[] = {
        "x + y - 3",
        "x - y + 1"
    };
    
    printf("\nTest 1: Linear system\n");
    rational_solutions_t *sols1 = solve_rational_polynomial_system_array(test1, 2);
    if (sols1) {
        print_rational_solutions(sols1);
        rational_solutions_clear(sols1);
        free(sols1);
    }
    
    char *test2[] = {
        "x^2 - 2",
        "x - 1"
    };
    
    printf("\nTest 2: Quadratic with no rational solution\n");
    rational_solutions_t *sols2 = solve_rational_polynomial_system_array(test2, 2);
    if (sols2) {
        print_rational_solutions(sols2);
        rational_solutions_clear(sols2);
        free(sols2);
    }
    
    printf("\n=== Test Complete ===\n");
}

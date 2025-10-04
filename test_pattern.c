// Quick test to verify our pattern logic
#include <stdio.h>
#include <stdbool.h>

#define GRID_SIZE 8

int main() {
    bool grid[GRID_SIZE][GRID_SIZE] = {0};

    // Test pattern from instantiate()
    grid[0][0] = true;  // Step 0, Note C2 (36)
    grid[0][4] = true;  // Step 0, Note E2 (40)
    grid[0][7] = true;  // Step 0, Note G2 (43)
    grid[4][0] = true;  // Step 4, Note C2 (36)
    grid[4][4] = true;  // Step 4, Note E2 (40)
    grid[4][7] = true;  // Step 4, Note G2 (43)

    printf("Pattern visualization (X = column/step, Y = row/note):\n");
    printf("    Steps: 0 1 2 3 4 5 6 7\n");
    for (int y = GRID_SIZE - 1; y >= 0; y--) {
        printf("Note %2d: ", 36 + y);
        for (int x = 0; x < GRID_SIZE; x++) {
            printf("%c ", grid[x][y] ? 'X' : '.');
        }
        printf("\n");
    }

    return 0;
}

filename = "results"
output_name = "500generated"

best_configs = []
near_best_configs = []

with open(filename, "r") as file:
    data = []
    for line in file:
        parts = line.strip().split()
        ef, af, be, score = int(parts[0]), int(parts[1]), int(parts[2]), float(parts[3])
        data.append((ef, af, be, score))

max_score = max(row[3] for row in data)

print(f"\nMax QoS Score: {max_score:.6f}")
print("\nBest configurations:")
for row in data:
    if row[3] == max_score:
        print(f"EF={row[0]}, AF={row[1]}, BE={row[2]} → Score={row[3]:.6f}")
        best_configs.append(row)

print("\nConfigurations <= 10% below max score:")
threshold = 0.95 * max_score
for row in data:
    if threshold <= row[3] < max_score:
        print(f"EF={row[0]}, AF={row[1]}, BE={row[2]} → Score={row[3]:.6f}")
        near_best_configs.append(row)

with open(f"best_{output_name}", "w") as best_out, open(f"near_best_{output_name}", "w") as near_out:
    for row in data:
        ef, af, be, score = row
        line = f"{ef} {af} {be}\n"
        if score == max_score:
            best_out.write(line)
        elif threshold <= score < max_score:
            near_out.write(line)

export const ranks = "AKQJT98765432";
export const handClasses = Array.from({ length: 13 }, (_, row) =>
  Array.from({ length: 13 }, (_, col) => row === col ? `${ranks[row]}${ranks[col]}` :
    row < col ? `${ranks[row]}${ranks[col]}s` : `${ranks[col]}${ranks[row]}o`)
).flat();

export function nextHandIndex(index: number, key: string): number {
  const row = Math.floor(index / 13);
  const col = index % 13;
  switch (key) {
    case "ArrowRight": return row * 13 + Math.min(12, col + 1);
    case "ArrowLeft": return row * 13 + Math.max(0, col - 1);
    case "ArrowDown": return Math.min(12, row + 1) * 13 + col;
    case "ArrowUp": return Math.max(0, row - 1) * 13 + col;
    case "Home": return row * 13;
    case "End": return row * 13 + 12;
    default: return index;
  }
}

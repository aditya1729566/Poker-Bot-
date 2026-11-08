export function localAdapterOrigin(raw: string | undefined): string | null {
  if (!raw) return null;
  const url = new URL(raw);
  if (!["127.0.0.1", "localhost", "[::1]"].includes(url.hostname) ||
      !["http:", "https:"].includes(url.protocol) || url.username || url.password ||
      url.pathname !== "/" || url.search || url.hash) {
    throw new Error("ARENA_ADAPTER_URL must be a loopback origin without credentials or a path.");
  }
  return url.origin;
}

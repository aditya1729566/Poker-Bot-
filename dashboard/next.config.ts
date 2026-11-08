import type { NextConfig } from "next";
const nextConfig: NextConfig = {
  reactStrictMode: true,
  async rewrites() {
    const adapter = process.env.ARENA_ADAPTER_URL;
    if (!adapter) return [];
    const url = new URL(adapter);
    if (!["127.0.0.1", "localhost", "[::1]"].includes(url.hostname) ||
        !["http:", "https:"].includes(url.protocol) || url.username || url.password ||
        url.pathname !== "/" || url.search || url.hash) {
      throw new Error("ARENA_ADAPTER_URL must be a loopback origin without credentials or a path.");
    }
    return { beforeFiles: [{ source: "/api/arena/v1/:path*", destination: `${url.origin}/api/arena/v1/:path*` }], afterFiles: [], fallback: [] };
  }
};
export default nextConfig;

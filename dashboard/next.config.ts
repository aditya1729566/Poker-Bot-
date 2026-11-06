import type { NextConfig } from "next";
const nextConfig: NextConfig = {
  reactStrictMode: true,
  async rewrites() {
    const adapter = process.env.ARENA_ADAPTER_URL;
    return adapter ? { beforeFiles: [{ source: "/api/arena/v1/:path*", destination: `${adapter.replace(/\/$/, "")}/api/arena/v1/:path*` }], afterFiles: [], fallback: [] } : [];
  }
};
export default nextConfig;

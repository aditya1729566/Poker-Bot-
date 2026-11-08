import { NextRequest, NextResponse } from "next/server";
import { localAdapterOrigin } from "@/lib/adapter-url";

type Context = { params: Promise<{ path: string[] }> };

function unavailable() {
  return NextResponse.json({ apiVersion: "arena.v1", message: "Local Arena adapter is not configured. Start the trusted C++ adapter and set ARENA_ADAPTER_URL." }, { status: 503 });
}

async function proxy(request: NextRequest, context: Context) {
  let origin: string | null;
  try { origin = localAdapterOrigin(process.env.ARENA_ADAPTER_URL); }
  catch { return NextResponse.json({ apiVersion: "arena.v1", message: "Arena adapter URL must be a local loopback origin." }, { status: 503 }); }
  if (!origin) return unavailable();

  const { path } = await context.params;
  if (!path?.length || path.some(segment => !segment || segment === "." || segment === "..") || request.nextUrl.search) {
    return NextResponse.json({ apiVersion: "arena.v1", message: "Invalid Arena path." }, { status: 422 });
  }
  const target = `${origin}/api/arena/v1/${path.map(encodeURIComponent).join("/")}`;
  const headers = new Headers();
  const cookie = request.headers.get("cookie")?.split(";").map(x => x.trim()).find(x => x.startsWith("pz_arena="));
  if (cookie) headers.set("cookie", cookie);
  const browserOrigin = request.headers.get("origin");
  if (browserOrigin) headers.set("origin", browserOrigin);
  let body: string | undefined;
  if (request.method === "POST") {
    body = await request.text();
    if (body.length > 4096) return NextResponse.json({ apiVersion: "arena.v1", message: "Arena request is too large." }, { status: 422 });
    headers.set("content-type", "application/json");
  }
  try {
    const upstream = await fetch(target, { method: request.method, headers, body, cache: "no-store", redirect: "manual" });
    const responseHeaders = new Headers({ "content-type": "application/json; charset=utf-8", "cache-control": "no-store", "x-content-type-options": "nosniff" });
    const setCookie = upstream.headers.get("set-cookie");
    if (setCookie) responseHeaders.set("set-cookie", setCookie);
    return new Response(await upstream.text(), { status: upstream.status, headers: responseHeaders });
  } catch {
    return NextResponse.json({ apiVersion: "arena.v1", message: "Local Arena adapter is unavailable." }, { status: 503 });
  }
}

export async function GET(request: NextRequest, context: Context) { return proxy(request, context); }
export async function POST(request: NextRequest, context: Context) { return proxy(request, context); }

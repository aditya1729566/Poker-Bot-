import { NextResponse } from "next/server";

function unavailable() {
  return NextResponse.json({ apiVersion: "arena.v1", message: "Local Arena adapter is not configured. Start the trusted C++ adapter and set ARENA_ADAPTER_URL." }, { status: 503 });
}
export const GET = unavailable;
export const POST = unavailable;

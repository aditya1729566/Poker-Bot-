import type { Metadata } from "next";
import Link from "next/link";
import { ChartNoAxesCombined, Layers3, Spade } from "lucide-react";
import "./globals.css";

export const metadata: Metadata = { title: "PokerZero Arena", description: "Offline heads-up poker research arena" };

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return <html lang="en"><body><div className="app-shell">
    <header className="topbar"><Link href="/" className="brand" aria-label="PokerZero home"><span className="brand-mark">P<span>0</span></span><span>POKERZERO<span className="brand-sub">RESEARCH ARENA</span></span></Link>
      <nav aria-label="Main navigation" className="nav"><Link href="/"><Spade size={15}/> Play</Link><Link href="/study"><Layers3 size={15}/> Study</Link><Link href="/research"><ChartNoAxesCombined size={15}/> Research</Link></nav>
      <span className="offline-tag">OFFLINE · LOCAL</span></header>{children}
  </div></body></html>;
}

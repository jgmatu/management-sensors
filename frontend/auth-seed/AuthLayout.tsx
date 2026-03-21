import type { ReactNode } from "react";

/**
 * Shell layout for the public authentication area.
 * Replace by editing `private/components/auth/AuthLayout.tsx` after copying from seed.
 */
export function AuthLayout({ children }: { children: ReactNode }) {
  return (
    <div className="flex min-h-screen flex-col bg-zinc-100 text-zinc-900 dark:bg-zinc-950 dark:text-zinc-50">
      <header className="border-b border-zinc-200/80 bg-white/80 px-6 py-4 backdrop-blur dark:border-zinc-800 dark:bg-zinc-900/80">
        <span className="text-sm font-semibold tracking-tight">Management Sensors</span>
      </header>
      <main className="flex flex-1 flex-col items-center justify-center px-4 py-16">
        {children}
      </main>
      <footer className="border-t border-zinc-200/80 py-4 text-center text-xs text-zinc-500 dark:border-zinc-800 dark:text-zinc-400">
        Vista de autenticación (plantilla vacía)
      </footer>
    </div>
  );
}

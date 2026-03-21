This is a [Next.js](https://nextjs.org) project bootstrapped with [`create-next-app`](https://nextjs.org/docs/app/api-reference/cli/create-next-app).

## Private UI & auth entry

- **`pnpm dev` / `pnpm build`** run `scripts/setup-components.sh`, which:
  1. Ensures `../private/components/auth/` exists and seeds **`AuthLayout.tsx`** / **`AuthPageTemplate.tsx`** from `auth-seed/` if those files are not already in `private/`.
  2. Copies **`../private/components/`** → **`src/components/`** (full sync).
- The **landing route** is **`/`** — empty authentication shell (`src/app/(auth)/`) using the layout/template from `private/components/auth/` (via the copy).
- The **dashboard** lives under **`(dashboard)/`** (e.g. `/sensors/`). You still need the rest of your Catalyst / proprietary components under `../private/components/` for those routes to build.

## Getting Started

```bash
pnpm install
pnpm dev
```

Open [http://localhost:3000/](http://localhost:3000/) — you should see the empty auth template first.

You can edit the auth shell in **`private/components/auth/`** (or the tracked defaults under **`auth-seed/`** before first copy).

This project uses [`next/font`](https://nextjs.org/docs/app/building-your-application/optimizing/fonts) to automatically optimize and load [Geist](https://vercel.com/font), a new font family for Vercel.

## Learn More

To learn more about Next.js, take a look at the following resources:

- [Next.js Documentation](https://nextjs.org/docs) - learn about Next.js features and API.
- [Learn Next.js](https://nextjs.org/learn) - an interactive Next.js tutorial.

You can check out [the Next.js GitHub repository](https://github.com/vercel/next.js) - your feedback and contributions are welcome!

## Deploy on Vercel

The easiest way to deploy your Next.js app is to use the [Vercel Platform](https://vercel.com/new?utm_medium=default-template&filter=next.js&utm_source=create-next-app&utm_campaign=create-next-app-readme) from the creators of Next.js.

Check out our [Next.js deployment documentation](https://nextjs.org/docs/app/building-your-application/deploying) for more details.

"use client"

import Image from "next/image";
import { Alert, AlertActions, AlertBody, AlertDescription, AlertTitle } from "../components/alert";
import { useState } from "react";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "../components/table";
import { Avatar } from "../components/avatar";
import { Button } from "../components/button";

export default function Home() {
  const [isOpen, setIsOpen] = useState(false);

  return (
    <div className="flex min-h-screen items-center justify-center bg-zinc-50 font-sans dark:bg-black">
      <main className="flex min-h-screen w-full max-w-3xl flex-col items-center justify-between py-32 px-16 bg-white dark:bg-black sm:items-start">
        <Image
          className="dark:invert"
          src="default/next.svg"
          alt="Next.js logo"
          width={100}
          height={20}
          priority
        />
        <div className="flex flex-col items-center gap-6 text-center sm:items-start sm:text-left">
          <h1 className="max-w-xs text-3xl font-semibold leading-10 tracking-tight text-black dark:text-zinc-50">
            To get started, edit the page.tsx file. Reload to see your changes.
          </h1>
          <p className="max-w-md text-lg leading-8 text-zinc-600 dark:text-zinc-400">
            Looking for a starting point or more instructions? Head over to{" "}
            <a
              href="https://vercel.com/templates?framework=next.js&utm_source=create-next-app&utm_medium=appdir-template-tw&utm_campaign=create-next-app"
              className="font-medium text-zinc-950 dark:text-zinc-50"
            >
              Templates
            </a>{" "}
            or the{" "}
            <a
              href="https://nextjs.org/learn?utm_source=create-next-app&utm_medium=appdir-template-tw&utm_campaign=create-next-app"
              className="font-medium text-zinc-950 dark:text-zinc-50"
            >
              Learning
            </a>{" "}
            center.
          </p>
        </div>
        <div className="flex flex-col gap-4 text-base font-medium sm:flex-row">
          <a
            className="flex h-12 w-full items-center justify-center gap-2 rounded-full bg-foreground px-5 text-background transition-colors hover:bg-[#383838] dark:hover:bg-[#ccc] md:w-[158px]"
            href="https://vercel.com/new?utm_source=create-next-app&utm_medium=appdir-template-tw&utm_campaign=create-next-app"
            target="_blank"
            rel="noopener noreferrer"
          >
            <Image
              className="dark:invert"
              src="default/vercel.svg"
              alt="Vercel logomark"
              width={16}
              height={16}
            />
            Deploy Now
          </a>
          <a
            className="flex h-12 w-full items-center justify-center rounded-full border border-solid border-black/[.08] px-5 transition-colors hover:border-transparent hover:bg-black/[.04] dark:border-white/[.145] dark:hover:bg-[#1a1a1a] md:w-[158px]"
            href="https://nextjs.org/docs?utm_source=create-next-app&utm_medium=appdir-template-tw&utm_campaign=create-next-app"
            target="_blank"
            rel="noopener noreferrer"
          >
            Documentation
          </a>
        </div>
          <div className="flex flex-col gap-4 text-base font-medium sm:flex-row">
            {/* Create Alert! */}
            <button onClick={() => setIsOpen(true)} className="fixed bottom-4 right-4 bg-blue-500 text-white p-2 rounded">
                Open Alert
            </button>

            <Alert open={isOpen} onClose={() => setIsOpen(false)}>
              <AlertTitle>Warning</AlertTitle>
              <AlertDescription>Your layout is now fixed!</AlertDescription>
              <AlertBody>
                <p>This is a simple alert body.</p>
              </AlertBody>
              <AlertActions>
                <button onClick={() => setIsOpen(false)}>Close</button>
              </AlertActions>
            </Alert>
            <Table className="mt-4 [--gutter:--spacing(6)] lg:[--gutter:--spacing(10)]">
              <TableHead>
                <TableRow>
                  <TableHeader>Order number</TableHeader>
                  <TableHeader>Purchase date</TableHeader>
                  <TableHeader>Customer</TableHeader>
                  <TableHeader>Event</TableHeader>
                  <TableHeader className="text-right">Amount</TableHeader>
                </TableRow>
              </TableHead>
              <TableBody>
                <TableRow key="01" href="#" title="View order details">
                  <TableCell>01</TableCell>
                  <TableCell className="text-zinc-500">10-11-2023</TableCell>
                  <TableCell>Order Article</TableCell>
                  <TableCell>
                    <div className="flex items-center gap-2">
                      <Avatar src="flags/us.svg" className="size-6" />
                      <span>Event Name</span>
                    </div>
                  </TableCell>
                  <TableCell className="text-right">100.00$</TableCell>
                </TableRow>
                <TableRow key="02" href="#" title="View order details">
                  <TableCell>02</TableCell>
                  <TableCell className="text-zinc-500">10-11-2023</TableCell>
                  <TableCell>Order Article</TableCell>
                  <TableCell>
                    <div className="flex items-center gap-2">
                      <Avatar src="flags/ca.svg" className="size-6" />
                      <span>Event Name</span>
                    </div>
                  </TableCell>
                  <TableCell className="text-right">50.00$</TableCell>
                </TableRow>
                <TableRow key="03" href="#" title="View order details">
                  <TableCell>03</TableCell>
                  <TableCell className="text-zinc-500">10-11-2023</TableCell>
                  <TableCell>Order Article</TableCell>
                  <TableCell>
                    <div className="flex items-center gap-2">
                      <Avatar src="flags/mx.svg" className="size-6" />
                      <span>Event Name</span>
                    </div>
                  </TableCell>
                  <TableCell className="text-right">20.00$</TableCell>
                </TableRow>
              </TableBody>
            </Table>
          </div>
          <div className="flex gap-4 p-8">
              <Button href="/errors" color="red">
                  Errors
              </Button>
              <Button href="/configuration" color="blue">
                  Configuration
              </Button>
              <Button href="/telemetry" color="zinc">
                  Telemetry
              </Button>
          </div>
      </main>
    </div>
  );
}

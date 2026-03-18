import type { ReactNode } from "react";
import { StackedLayout } from "../../components/stacked-layout";
import { Button } from "../../components/button";

export default function DashboardLayout({ children }: { children: ReactNode }) {
    return (
        <StackedLayout
            navbar={
                <div className="flex items-center gap-3 py-2">
                    <Button href="/" outline>
                        Back to Home
                    </Button>
                    <div className="font-semibold">Dashboard</div>
                </div>
            }
            sidebar={<div className="p-4">Sidebar</div>}
        >
            {children}
        </StackedLayout>
    );
}
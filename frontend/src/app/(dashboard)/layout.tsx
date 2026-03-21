import type { ReactNode } from "react";
import { StackedLayout } from "../../components/stacked-layout";
import { Navbar, NavbarItem, NavbarSection, NavbarSpacer } from "../../components/navbar";

export default function DashboardLayout({ children }: { children: ReactNode }) {
    return (
        <StackedLayout
            navbar={
                <Navbar>
                    <NavbarSection>
                        <NavbarItem href="/">Inicio</NavbarItem>
                        <NavbarItem href="/sensors/">Sensores</NavbarItem>
                        <NavbarItem href="/telemetry/">Telemetría</NavbarItem>
                        <NavbarItem href="/configuration/">Configuración</NavbarItem>
                        <NavbarItem href="/errors/">Errores</NavbarItem>
                    </NavbarSection>
                    <NavbarSpacer />
                </Navbar>
            }
            sidebar={<div className="p-4">Sidebar</div>}
        >
            {children}
        </StackedLayout>
    );
}
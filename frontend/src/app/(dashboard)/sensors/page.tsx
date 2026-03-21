'use client'

import { useState } from 'react'
import { Heading } from '../../../components/heading'
import { Button } from '../../../components/button'
import { Badge } from '../../../components/badge'
import {
    Table,
    TableHead,
    TableBody,
    TableRow,
    TableHeader,
    TableCell,
} from '../../../components/table'
import {
    Dialog,
    DialogTitle,
    DialogDescription,
    DialogBody,
    DialogActions,
} from '../../../components/dialog'
import {
    Alert,
    AlertTitle,
    AlertDescription,
    AlertActions,
} from '../../../components/alert'
import { Field, FieldGroup, Label } from '../../../components/fieldset'
import { Input } from '../../../components/input'
import { Select } from '../../../components/select'

interface Sensor {
    id: number
    hostname: string
    ip: string
    isActive: boolean
    lastUpdate: string
    temp: number | null
}

const initialSensors: Sensor[] = [
    { id: 1, hostname: 'sensor-1', ip: '192.168.1.10/24', isActive: true, lastUpdate: '2026-03-20 14:32:01', temp: 42.5 },
    { id: 2, hostname: 'sensor-2', ip: '192.168.1.11/24', isActive: true, lastUpdate: '2026-03-20 14:31:58', temp: 38.1 },
    { id: 3, hostname: 'sensor-3', ip: '10.0.0.5/16', isActive: false, lastUpdate: '2026-03-19 09:15:22', temp: null },
    { id: 4, hostname: 'sensor-4', ip: '172.16.0.100/24', isActive: true, lastUpdate: '2026-03-20 14:32:00', temp: 55.7 },
    { id: 5, hostname: 'sensor-5', ip: '192.168.2.20/24', isActive: false, lastUpdate: '2026-03-18 22:01:45', temp: null },
]

const emptySensorForm = { hostname: '', ip: '', isActive: true }

export default function SensorsPage() {
    const [sensors, setSensors] = useState<Sensor[]>(initialSensors)

    const [addOpen, setAddOpen] = useState(false)
    const [addForm, setAddForm] = useState(emptySensorForm)

    const [editOpen, setEditOpen] = useState(false)
    const [editSensor, setEditSensor] = useState<Sensor | null>(null)
    const [editForm, setEditForm] = useState(emptySensorForm)

    const [deleteOpen, setDeleteOpen] = useState(false)
    const [deleteSensor, setDeleteSensor] = useState<Sensor | null>(null)

    function handleAdd() {
        const nextId = Math.max(0, ...sensors.map(s => s.id)) + 1
        setSensors(prev => [
            ...prev,
            {
                id: nextId,
                hostname: addForm.hostname || `sensor-${nextId}`,
                ip: addForm.ip,
                isActive: addForm.isActive,
                lastUpdate: new Date().toISOString().replace('T', ' ').slice(0, 19),
                temp: null,
            },
        ])
        setAddForm(emptySensorForm)
        setAddOpen(false)
    }

    function openEdit(sensor: Sensor) {
        setEditSensor(sensor)
        setEditForm({ hostname: sensor.hostname, ip: sensor.ip, isActive: sensor.isActive })
        setEditOpen(true)
    }

    function handleEdit() {
        if (!editSensor) return
        setSensors(prev =>
            prev.map(s =>
                s.id === editSensor.id
                    ? { ...s, hostname: editForm.hostname, ip: editForm.ip, isActive: editForm.isActive }
                    : s
            )
        )
        setEditOpen(false)
        setEditSensor(null)
    }

    function openDelete(sensor: Sensor) {
        setDeleteSensor(sensor)
        setDeleteOpen(true)
    }

    function handleDelete() {
        if (!deleteSensor) return
        setSensors(prev => prev.filter(s => s.id !== deleteSensor.id))
        setDeleteOpen(false)
        setDeleteSensor(null)
    }

    return (
        <div className="p-6 lg:p-10">
            <div className="flex items-center justify-between mb-8">
                <Heading>Sensores</Heading>
                <Button color="indigo" onClick={() => setAddOpen(true)}>
                    <PlusIcon />
                    Añadir sensor
                </Button>
            </div>

            <Table striped>
                <TableHead>
                    <TableRow>
                        <TableHeader>ID</TableHeader>
                        <TableHeader>Hostname</TableHeader>
                        <TableHeader>IP</TableHeader>
                        <TableHeader>Estado</TableHeader>
                        <TableHeader>Temperatura</TableHeader>
                        <TableHeader>Última actualización</TableHeader>
                        <TableHeader className="text-right">Acciones</TableHeader>
                    </TableRow>
                </TableHead>
                <TableBody>
                    {sensors.map(sensor => (
                        <TableRow key={sensor.id}>
                            <TableCell className="font-mono">{sensor.id}</TableCell>
                            <TableCell className="font-medium">{sensor.hostname}</TableCell>
                            <TableCell className="font-mono">{sensor.ip}</TableCell>
                            <TableCell>
                                <Badge color={sensor.isActive ? 'green' : 'zinc'}>
                                    {sensor.isActive ? 'Activo' : 'Inactivo'}
                                </Badge>
                            </TableCell>
                            <TableCell className="font-mono">
                                {sensor.temp !== null ? `${sensor.temp.toFixed(1)} °C` : '—'}
                            </TableCell>
                            <TableCell className="text-zinc-500">{sensor.lastUpdate}</TableCell>
                            <TableCell className="text-right">
                                <div className="flex justify-end gap-2">
                                    <Button plain onClick={() => openEdit(sensor)}>
                                        <PencilIcon />
                                    </Button>
                                    <Button plain onClick={() => openDelete(sensor)}>
                                        <TrashIcon />
                                    </Button>
                                </div>
                            </TableCell>
                        </TableRow>
                    ))}
                    {sensors.length === 0 && (
                        <TableRow>
                            <TableCell colSpan={7} className="text-center text-zinc-400 py-12">
                                No hay sensores registrados.
                            </TableCell>
                        </TableRow>
                    )}
                </TableBody>
            </Table>

            {/* Add sensor dialog */}
            <Dialog open={addOpen} onClose={setAddOpen} size="md">
                <DialogTitle>Añadir sensor</DialogTitle>
                <DialogDescription>
                    Introduce los datos del nuevo sensor. El ID se asigna automáticamente.
                </DialogDescription>
                <DialogBody>
                    <FieldGroup>
                        <Field>
                            <Label>Hostname</Label>
                            <Input
                                placeholder="sensor-6"
                                value={addForm.hostname}
                                onChange={e => setAddForm(f => ({ ...f, hostname: e.target.value }))}
                            />
                        </Field>
                        <Field>
                            <Label>Dirección IP</Label>
                            <Input
                                placeholder="192.168.1.100/24"
                                value={addForm.ip}
                                onChange={e => setAddForm(f => ({ ...f, ip: e.target.value }))}
                            />
                        </Field>
                        <Field>
                            <Label>Estado</Label>
                            <Select
                                value={addForm.isActive ? 'active' : 'inactive'}
                                onChange={e => setAddForm(f => ({ ...f, isActive: e.target.value === 'active' }))}
                            >
                                <option value="active">Activo</option>
                                <option value="inactive">Inactivo</option>
                            </Select>
                        </Field>
                    </FieldGroup>
                </DialogBody>
                <DialogActions>
                    <Button plain onClick={() => setAddOpen(false)}>Cancelar</Button>
                    <Button color="indigo" onClick={handleAdd}>Añadir</Button>
                </DialogActions>
            </Dialog>

            {/* Edit sensor dialog */}
            <Dialog open={editOpen} onClose={setEditOpen} size="md">
                <DialogTitle>Configurar sensor #{editSensor?.id}</DialogTitle>
                <DialogDescription>
                    Modifica la configuración del sensor. Los cambios se aplicarán vía el pipeline CONFIG_IP.
                </DialogDescription>
                <DialogBody>
                    <FieldGroup>
                        <Field>
                            <Label>Hostname</Label>
                            <Input
                                value={editForm.hostname}
                                onChange={e => setEditForm(f => ({ ...f, hostname: e.target.value }))}
                            />
                        </Field>
                        <Field>
                            <Label>Dirección IP</Label>
                            <Input
                                value={editForm.ip}
                                onChange={e => setEditForm(f => ({ ...f, ip: e.target.value }))}
                            />
                        </Field>
                        <Field>
                            <Label>Estado</Label>
                            <Select
                                value={editForm.isActive ? 'active' : 'inactive'}
                                onChange={e => setEditForm(f => ({ ...f, isActive: e.target.value === 'active' }))}
                            >
                                <option value="active">Activo</option>
                                <option value="inactive">Inactivo</option>
                            </Select>
                        </Field>
                    </FieldGroup>
                </DialogBody>
                <DialogActions>
                    <Button plain onClick={() => setEditOpen(false)}>Cancelar</Button>
                    <Button color="indigo" onClick={handleEdit}>Guardar</Button>
                </DialogActions>
            </Dialog>

            {/* Delete confirmation */}
            <Alert open={deleteOpen} onClose={setDeleteOpen}>
                <AlertTitle>Eliminar sensor #{deleteSensor?.id}</AlertTitle>
                <AlertDescription>
                    ¿Estás seguro de que quieres eliminar <strong>{deleteSensor?.hostname}</strong> ({deleteSensor?.ip})?
                    Esta acción no se puede deshacer.
                </AlertDescription>
                <AlertActions>
                    <Button plain onClick={() => setDeleteOpen(false)}>Cancelar</Button>
                    <Button color="red" onClick={handleDelete}>Eliminar</Button>
                </AlertActions>
            </Alert>
        </div>
    )
}

function PlusIcon() {
    return (
        <svg data-slot="icon" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor">
            <path d="M10.75 4.75a.75.75 0 0 0-1.5 0v4.5h-4.5a.75.75 0 0 0 0 1.5h4.5v4.5a.75.75 0 0 0 1.5 0v-4.5h4.5a.75.75 0 0 0 0-1.5h-4.5v-4.5Z" />
        </svg>
    )
}

function PencilIcon() {
    return (
        <svg data-slot="icon" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor">
            <path d="m5.433 13.917 1.262-3.155A4 4 0 0 1 7.58 9.42l6.92-6.918a2.121 2.121 0 0 1 3 3l-6.92 6.918c-.383.383-.84.685-1.343.886l-3.154 1.262a.5.5 0 0 1-.65-.65Z" />
            <path d="M3.5 5.75c0-.69.56-1.25 1.25-1.25H10A.75.75 0 0 0 10 3H4.75A2.75 2.75 0 0 0 2 5.75v9.5A2.75 2.75 0 0 0 4.75 18h9.5A2.75 2.75 0 0 0 17 15.25V10a.75.75 0 0 0-1.5 0v5.25c0 .69-.56 1.25-1.25 1.25h-9.5c-.69 0-1.25-.56-1.25-1.25v-9.5Z" />
        </svg>
    )
}

function TrashIcon() {
    return (
        <svg data-slot="icon" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor">
            <path fillRule="evenodd" d="M8.75 1A2.75 2.75 0 0 0 6 3.75v.443c-.795.077-1.584.176-2.365.298a.75.75 0 1 0 .23 1.482l.149-.022.841 10.518A2.75 2.75 0 0 0 7.596 19h4.807a2.75 2.75 0 0 0 2.742-2.53l.841-10.52.149.023a.75.75 0 0 0 .23-1.482A41.03 41.03 0 0 0 14 4.193V3.75A2.75 2.75 0 0 0 11.25 1h-2.5ZM10 4c.84 0 1.673.025 2.5.075V3.75c0-.69-.56-1.25-1.25-1.25h-2.5c-.69 0-1.25.56-1.25 1.25v.325C8.327 4.025 9.16 4 10 4ZM8.58 7.72a.75.75 0 0 0-1.5.06l.3 7.5a.75.75 0 1 0 1.5-.06l-.3-7.5Zm4.34.06a.75.75 0 1 0-1.5-.06l-.3 7.5a.75.75 0 1 0 1.5.06l.3-7.5Z" clipRule="evenodd" />
        </svg>
    )
}

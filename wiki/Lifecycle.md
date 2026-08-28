# Lifecycle

Task lifecycle is deliberately explicit.

## Construction

Constructors configure objects but do not execute user work.

## Initialize

`TaskExecutor::Initialize()` reserves queue and worker resources and installs the work handler. The worker remains behind its start gate.

## Start

`Start()` opens the gate and allows queued/submitted work to execute.

This separation is important during embedded startup because it lets applications allocate execution resources early while still controlling when asynchronous work can observe other services.

## Teardown

A consumer or higher-level library should stop/destroy Task resources deterministically rather than relying on process termination. Extensions must preserve the ordering guarantees around queue, worker, and synchronization resource lifetime.
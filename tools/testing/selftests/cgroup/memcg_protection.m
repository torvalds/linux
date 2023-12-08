% SPDX-License-Identifier: GPL-2.0
%
% run as: octave-cli memcg_protection.m
%
% This script simulates reclaim protection behavior on a single level of memcg
% hierarchy to illustrate how overcommitted protection spreads among siblings
% (as it depends also on their current consumption).
%
% Simulation assumes siblings consumed the initial amount of memory (w/out
% reclaim) and then the reclaim starts, all memory is reclaimable, i.e. treated
% same. It simulates only non-low reclaim and assumes all memory.min = 0.
%
% Input configurations
% --------------------
% E number	parent effective protection
% n vector	nominal protection of siblings set at the given level (memory.low)
% c vector	current consumption -,,- (memory.current)

% example from testcase (values in GB)
E = 50 / 1024;
n = [75 25 0 500 ] / 1024;
c = [50 50 50 0] / 1024;

% Reclaim parameters
% ------------------

% Minimal reclaim amount (GB)
cluster = 32*4 / 2**20;

% Reclaim coefficient (think as 0.5^sc->priority)
alpha = .1

% Simulation parameters
% ---------------------
epsilon = 1e-7;
timeout = 1000;

% Simulation loop
% ---------------

ch = [];
eh = [];
rh = [];

for t = 1:timeout
        % low_usage
        u = min(c, n);
        siblings = sum(u);

        % effective_protection()
        protected = min(n, c);                % start with nominal
        e = protected * min(1, E / siblings); % normalize overcommit

        % recursive protection
        unclaimed = max(0, E - siblings);
        parent_overuse = sum(c) - siblings;
        if (unclaimed > 0 && parent_overuse > 0)
                overuse = max(0, c - protected);
                e += unclaimed * (overuse / parent_overuse);
        endif

        % get_scan_count()
        r = alpha * c;             % assume all memory is in a single LRU list

        % commit 1bc63fb1272b ("mm, memcg: make scan aggression always exclude protection")
        sz = max(e, c);
        r .*= (1 - (e+epsilon) ./ (sz+epsilon));

        % uncomment to debug prints
        % e, c, r

        % nothing to reclaim, reached equilibrium
        if max(r) < epsilon
                break;
        endif

        % SWAP_CLUSTER_MAX roundup
        r = max(r, (r > epsilon) .* cluster);
        % XXX here I do parallel reclaim of all siblings
        % in reality reclaim is serialized and each sibling recalculates own residual
        c = max(c - r, 0);

        ch = [ch ; c];
        eh = [eh ; e];
        rh = [rh ; r];
endfor

t
c, e

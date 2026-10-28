package com.example.stratumcppdemo;

import com.stratum.runtime.StratumActivity;

public class MainActivity extends StratumActivity {
    // Called by StratumActivity AFTER libstratum.so is loaded and BEFORE onCreate is
    // dispatched to native. Loading our lib here lets its static init wire the lifecycle.
    @Override
    protected boolean onImportMain() {
        System.loadLibrary("stratumcppdemo");
        return true;
    }
}
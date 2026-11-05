function snar_plot(run)
  % NaN only in local PLOT copies: never interpolate or change saved samples.
  breaks=[false;diff(run.segments)!=0]; a=run.f_B; w=run.omega_B; temperature=run.temperature;
  a(breaks,:)=NaN; w(breaks,:)=NaN; temperature(breaks)=NaN;
  figure; subplot(3,1,1); plot(run.t, a); ylabel('f_B, m/s^2'); legend('X','Y','Z'); grid on;
  subplot(3,1,2); plot(run.t, w); ylabel('omega_B, rad/s'); grid on;
  subplot(3,1,3); plot(run.t, temperature); ylabel('T, deg C'); xlabel('t ESP32, s'); grid on;
  q = run.samples(:,10:13);
  r = atan2(2*(q(:,1).*q(:,2)+q(:,3).*q(:,4)), 1-2*(q(:,2).^2+q(:,3).^2));
  p = asin(max(-1,min(1,2*(q(:,1).*q(:,3)-q(:,4).*q(:,2)))));
  y = atan2(2*(q(:,1).*q(:,4)+q(:,2).*q(:,3)), 1-2*(q(:,3).^2+q(:,4).^2));
  angles = [r,p,y]*180/pi; angles(bitand(run.flags,uint32(1))!=0,:) = NaN;
  angles(breaks,:)=NaN;
  figure; plot(run.t, angles); grid on; ylabel('deg'); xlabel('t ESP32, s'); legend('roll','pitch','relative yaw (drifts)');
  figure; dt=diff(run.samples(:,2)); plot(run.t(2:end),dt); ylabel('dt, us'); xlabel('t ESP32, s'); grid on;
endfunction
